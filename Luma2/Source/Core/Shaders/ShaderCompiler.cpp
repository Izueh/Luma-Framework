#include "Shaders/ShaderCompiler.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#include <Windows.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Log/Log.hpp"
#include "Shaders/ShaderContainer.hpp"
#include "System/Modules.hpp"
#include "System/Paths.hpp"
#include "Utils/Strings.hpp"

#pragma comment(lib, "version.lib")

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace Compiler {
namespace {
struct Library
{
   pD3DPreprocess preprocess = nullptr;
   pD3DCompile compile = nullptr;
   decltype(&D3DReflect) reflect = nullptr;
   pD3DDisassemble disassemble = nullptr;
};

struct DxcLibrary
{
   DxcCreateInstanceProc create_instance = nullptr;
};

Library library;
std::once_flag load_once;
DxcLibrary dxc_library;
std::once_flag dxc_load_once;
// FXC is used by one thread at a time: it isn't thread safe. Recursive, so a caller holds a Lock across the use of
// the reflector it got. Innermost: nothing else is locked while it's held, and each call holds it only for its own duration.
std::recursive_mutex fxc_mutex;

void LoadDxc()
{
   std::vector<std::filesystem::path> candidates = {
      System::Paths::GetRoot() / L"dxcompiler.dll",
      System::Modules::GetSelfDirectory() / L"dxcompiler.dll",
   };
#ifdef LUMA_RESOURCES_DIR
   candidates.push_back(std::filesystem::path(LUMA_RESOURCES_DIR).parent_path() / L"External" / L"Decompilers" / L"dxcompiler.dll");
#endif
   candidates.push_back(System::Modules::GetSystemPath(L"dxcompiler.dll"));
   HMODULE module = nullptr;
   for (const std::filesystem::path& path : candidates)
   {
      std::error_code error;
      if (std::filesystem::is_regular_file(path, error))
      {
         module = LoadLibraryW(path.c_str());
         if (module)
         {
            Log::Info("Shaders: loaded DXC from {}", Utils::Strings::ToUtf8(path));
            break;
         }
      }
   }
   if (!module)
      module = LoadLibraryW(L"dxcompiler.dll");
   if (module)
      dxc_library.create_instance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
}

const DxcLibrary& GetDxc()
{
   std::call_once(dxc_load_once, &LoadDxc);
   return dxc_library;
}

uint64_t GetProductVersion(const std::filesystem::path& path)
{
   DWORD handle = 0;
   const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
   if (size == 0)
      return 0;
   std::vector<uint8_t> info(size);
   VS_FIXEDFILEINFO* fixed = nullptr;
   UINT fixed_size = 0;
   if (!GetFileVersionInfoW(path.c_str(), 0, size, info.data()) || !VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size) ||
      !fixed)
      return 0;
   return (uint64_t(fixed->dwProductVersionMS) << 32) | fixed->dwProductVersionLS;
}

// A copy shipped in the Luma folder, for Wine and Proton users whose system one may be missing or outdated, is used
// when newer. Product versions, as System32's file versions follow another scheme.
void Load()
{
   const std::filesystem::path system = System::Modules::GetSystemPath(L"D3DCompiler_47.dll");
   const std::filesystem::path shipped = System::Paths::GetRoot() / L"D3DCompiler_47.dll";
   std::error_code error;
   const uint64_t shipped_version = std::filesystem::is_regular_file(shipped, error) ? GetProductVersion(shipped) : 0;
   const std::filesystem::path& path = shipped_version > GetProductVersion(system) ? shipped : system;
   const HMODULE module = LoadLibraryW(path.c_str());
   if (!module)
   {
      Log::Error("Shaders: {} couldn't be loaded ({}), no shader can be compiled", Utils::Strings::ToUtf8(path), GetLastError());
      return;
   }
   library.preprocess = reinterpret_cast<pD3DPreprocess>(GetProcAddress(module, "D3DPreprocess"));
   library.compile = reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile"));
   library.reflect = reinterpret_cast<decltype(&D3DReflect)>(GetProcAddress(module, "D3DReflect"));
   library.disassemble = reinterpret_cast<pD3DDisassemble>(GetProcAddress(module, "D3DDisassemble"));
   Log::Info("Shaders: compiling with {}", Utils::Strings::ToUtf8(path));
}

const Library& GetLibrary()
{
   std::call_once(load_once, &Load);
   return library;
}

std::optional<std::string> ReadFile(std::span<const std::filesystem::path> roots, std::string_view file)
{
   const std::filesystem::path path = System::Paths::Find(roots, std::filesystem::path(std::u8string(file.begin(), file.end())));
   if (path.empty())
      return std::nullopt;
   std::ifstream stream(path, std::ios::binary);
   if (!stream)
      return std::nullopt;
   return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Every #include through the roots, by its name from the Shaders root
struct Include final : ID3DInclude
{
   std::span<const std::filesystem::path> roots;

   explicit Include(std::span<const std::filesystem::path> roots_) : roots(roots_) {}

   HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override
   {
      const std::optional<std::string> text = ReadFile(roots, name);
      if (!text)
         return E_FAIL;
      char* copy = static_cast<char*>(std::malloc(text->size() + 1));
      std::memcpy(copy, text->c_str(), text->size() + 1);
      *data = copy;
      *size = UINT(text->size());
      return S_OK;
   }

   HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
   {
      std::free(const_cast<void*>(data));
      return S_OK;
   }
};

// FXC makes the source's name absolute with the current directory. The file is included by a one line source instead, so
// every #line and message names it from the Shaders root: only that line's #line carries a machine's path.
std::string GetSource(std::string_view file)
{
   return std::format("#include \"{}\"\n", file);
}

// Defines with an empty name or value are skipped
std::vector<D3D_SHADER_MACRO> GetMacros(std::span<const Define> defines)
{
   std::vector<D3D_SHADER_MACRO> macros;
   for (const Define& define : defines)
   {
      if (!define.name.empty() && !define.value.empty())
         macros.push_back({define.name.c_str(), define.value.c_str()});
   }
   macros.push_back({nullptr, nullptr});
   return macros;
}

std::string GetText(ID3DBlob* blob)
{
   return blob ? std::string(static_cast<const char*>(blob->GetBufferPointer()), strnlen(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize())) : std::string();
}
} // namespace

Lock::Lock()
{
   fxc_mutex.lock();
}

Lock::~Lock()
{
   fxc_mutex.unlock();
}

Preprocessed Preprocess(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Define> defines)
{
   const Library& fxc = GetLibrary();
   if (!fxc.preprocess)
      return {{}, "D3DCompiler_47.dll isn't loaded", true};
   const std::string source = GetSource(file);
   const std::string name(file);
   const std::vector<D3D_SHADER_MACRO> macros = GetMacros(defines);
   Include include(roots);
   // Before the blobs, which are released inside it
   const Lock lock;
   ComPtr<ID3DBlob> text;
   ComPtr<ID3DBlob> messages;
   const HRESULT result = fxc.preprocess(source.data(), source.size(), name.c_str(), macros.data(), &include, &text, &messages);
   return {GetText(text.Get()), GetText(messages.Get()), FAILED(result)};
}

Compiled Compile(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Define> defines, std::string_view target,
   std::string_view entry)
{
   const Library& fxc = GetLibrary();
   if (!fxc.compile)
      return {{}, "D3DCompiler_47.dll isn't loaded", true};
   // Optimization level 0 on purpose (another level changes every shader's bytecode), and backwards compatibility for shader model 3 and older
   UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL0;
   if (target.size() > 3 && target[3] <= '3')
      flags |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
   const std::string source = GetSource(file);
   const std::string name(file);
   const std::string entry_name(entry);
   const std::string target_name(target);
   const std::vector<D3D_SHADER_MACRO> macros = GetMacros(defines);
   Include include(roots);
   const Lock lock;
   ComPtr<ID3DBlob> code;
   ComPtr<ID3DBlob> messages;
   const HRESULT result =
      fxc.compile(source.data(), source.size(), name.c_str(), macros.data(), &include, entry_name.c_str(), target_name.c_str(), flags, 0, &code, &messages);
   Compiled compiled = {{}, GetText(messages.Get()), FAILED(result) || !code};
   if (!compiled.failed)
   {
      const auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
      compiled.bytecode.assign(bytes, bytes + code->GetBufferSize());
   }
   else if (compiled.messages.empty())
   {
      compiled.messages = std::format("D3DCompile failed (0x{:08X})", uint32_t(result));
   }
   return compiled;
}

bool Reflect(std::span<const uint8_t> bytecode, REFIID iid, void** reflector)
{
   const Library& fxc = GetLibrary();
   const Lock lock;
   return fxc.reflect && SUCCEEDED(fxc.reflect(bytecode.data(), bytecode.size(), iid, reflector));
}

std::optional<std::string> Disassemble(std::span<const uint8_t> bytecode)
{
   if (bytecode.empty())
      return std::nullopt;
   if (Container::HasChunk(bytecode, "DXIL"))
   {
      const DxcLibrary& dxc = GetDxc();
      if (!dxc.create_instance)
         return std::nullopt;
      ComPtr<IDxcCompiler3> compiler;
      if (FAILED(dxc.create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
         return std::nullopt;
      const DxcBuffer buffer = {.Ptr = bytecode.data(), .Size = bytecode.size(), .Encoding = 0};
      ComPtr<IDxcResult> result;
      if (FAILED(compiler->Disassemble(&buffer, IID_PPV_ARGS(&result))) || !result)
         return std::nullopt;
      ComPtr<IDxcBlobUtf8> text;
      if (FAILED(result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&text), nullptr)) || !text || !text->GetStringPointer())
         return std::nullopt;
      return std::string(text->GetStringPointer(), text->GetStringLength());
   }
   const Library& fxc = GetLibrary();
   const Lock lock;
   ComPtr<ID3DBlob> text;
   if (!fxc.disassemble ||
      FAILED(fxc.disassemble(bytecode.data(), bytecode.size(),
         D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS | D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING | D3D_DISASM_ENABLE_INSTRUCTION_OFFSET, nullptr, &text)))
      return std::nullopt;
   return GetText(text.Get());
}

std::vector<Binding> ReflectConstantBufferBindings(std::span<const uint8_t> bytecode)
{
   std::vector<Binding> bindings;
   const Lock lock;
   ComPtr<ID3D11ShaderReflection> reflector;
   D3D11_SHADER_DESC desc = {};
   if (!Reflect(bytecode, __uuidof(ID3D11ShaderReflection), &reflector) || FAILED(reflector->GetDesc(&desc)))
      return bindings;
   for (UINT i = 0; i < desc.BoundResources; i++)
   {
      D3D11_SHADER_INPUT_BIND_DESC binding = {};
      if (SUCCEEDED(reflector->GetResourceBindingDesc(i, &binding)) && binding.Type == D3D_SIT_CBUFFER && binding.Name)
         bindings.push_back({binding.Name, binding.BindPoint});
   }
   return bindings;
}

std::vector<Binding> ReflectTextureBindings(std::span<const uint8_t> bytecode)
{
   std::vector<Binding> bindings;
   const Lock lock;
   ComPtr<ID3D11ShaderReflection> reflector;
   D3D11_SHADER_DESC desc = {};
   if (!Reflect(bytecode, __uuidof(ID3D11ShaderReflection), &reflector) || FAILED(reflector->GetDesc(&desc)))
      return bindings;
   for (UINT i = 0; i < desc.BoundResources; i++)
   {
      D3D11_SHADER_INPUT_BIND_DESC binding = {};
      if (SUCCEEDED(reflector->GetResourceBindingDesc(i, &binding)) && binding.Type == D3D_SIT_TEXTURE && binding.Name)
         bindings.push_back({binding.Name, binding.BindPoint});
   }
   return bindings;
}

#if DEVELOPMENT
namespace {
Variable::Kind GetKind(D3D_SHADER_VARIABLE_TYPE type)
{
   switch (type)
   {
   case D3D_SVT_FLOAT: return Variable::Kind::Float;
   case D3D_SVT_INT: return Variable::Kind::Int;
   case D3D_SVT_UINT: return Variable::Kind::Uint;
   case D3D_SVT_BOOL: return Variable::Kind::Bool;
   default: return Variable::Kind::Other;
   }
}

// A variable of `type` at `offset`, or its members for a struct (an array of structs stays one raw variable)
template <typename TypeDesc, typename ReflectionType>
void AddVariables(ReflectionType* type, const std::string& name, uint32_t offset, uint32_t size, std::vector<Variable>& variables)
{
   TypeDesc desc = {};
   if (!type || FAILED(type->GetDesc(&desc)))
      return;
   if (desc.Class == D3D_SVC_STRUCT && desc.Elements == 0)
   {
      for (UINT i = 0; i < desc.Members; i++)
      {
         ReflectionType* member = type->GetMemberTypeByIndex(i);
         TypeDesc member_desc = {};
         if (!member || FAILED(member->GetDesc(&member_desc)))
            continue;
         // The member's size isn't given: until the next member, or the struct's end
         uint32_t member_size = size - member_desc.Offset;
         if (i + 1 < desc.Members)
         {
            TypeDesc next_desc = {};
            if (ReflectionType* next = type->GetMemberTypeByIndex(i + 1); next && SUCCEEDED(next->GetDesc(&next_desc)))
               member_size = next_desc.Offset - member_desc.Offset;
         }
         AddVariables<TypeDesc>(member, std::format("{}.{}", name, type->GetMemberTypeName(i)), offset + member_desc.Offset, member_size, variables);
      }
      return;
   }
   const bool numeric = desc.Class == D3D_SVC_SCALAR || desc.Class == D3D_SVC_VECTOR || desc.Class == D3D_SVC_MATRIX_ROWS || desc.Class == D3D_SVC_MATRIX_COLUMNS;
   variables.push_back({
      .name = name,
      .type = desc.Name ? desc.Name : "",
      .offset = offset,
      .size = size,
      .kind = numeric ? GetKind(desc.Type) : Variable::Kind::Other,
      .rows = desc.Rows,
      .columns = desc.Columns,
      .elements = desc.Elements,
      .column_major = desc.Class == D3D_SVC_MATRIX_COLUMNS,
   });
}

std::vector<ConstantBuffer> ReflectDxilConstantBuffers(std::span<const uint8_t> bytecode)
{
   std::vector<ConstantBuffer> buffers;
   const DxcLibrary& dxc = GetDxc();
   if (!dxc.create_instance)
      return buffers;
   ComPtr<IDxcUtils> utils;
   if (FAILED(dxc.create_instance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
      return buffers;
   const DxcBuffer container = {.Ptr = bytecode.data(), .Size = bytecode.size(), .Encoding = 0};
   ComPtr<ID3D12ShaderReflection> reflector;
   if (FAILED(utils->CreateReflection(&container, IID_PPV_ARGS(&reflector))))
      return buffers;
   D3D12_SHADER_DESC desc = {};
   if (FAILED(reflector->GetDesc(&desc)))
      return buffers;
   for (UINT i = 0; i < desc.ConstantBuffers; i++)
   {
      ID3D12ShaderReflectionConstantBuffer* buffer = reflector->GetConstantBufferByIndex(i);
      D3D12_SHADER_BUFFER_DESC buffer_desc = {};
      D3D12_SHADER_INPUT_BIND_DESC binding = {};
      if (!buffer || FAILED(buffer->GetDesc(&buffer_desc)) || buffer_desc.Type != D3D_CT_CBUFFER ||
         FAILED(reflector->GetResourceBindingDescByName(buffer_desc.Name, &binding)))
         continue;
      ConstantBuffer& reflected = buffers.emplace_back(buffer_desc.Name, binding.BindPoint, buffer_desc.Size);
      for (UINT j = 0; j < buffer_desc.Variables; j++)
      {
         ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex(j);
         D3D12_SHADER_VARIABLE_DESC variable_desc = {};
         if (variable && SUCCEEDED(variable->GetDesc(&variable_desc)))
            AddVariables<D3D12_SHADER_TYPE_DESC>(variable->GetType(), variable_desc.Name, variable_desc.StartOffset, variable_desc.Size, reflected.variables);
      }
   }
   return buffers;
}
} // namespace

std::vector<ConstantBuffer> ReflectConstantBuffers(std::span<const uint8_t> bytecode)
{
   if (Container::HasChunk(bytecode, "DXIL"))
      return ReflectDxilConstantBuffers(bytecode);
   std::vector<ConstantBuffer> buffers;
   const Lock lock;
   ComPtr<ID3D11ShaderReflection> reflector;
   D3D11_SHADER_DESC desc = {};
   if (!Reflect(bytecode, __uuidof(ID3D11ShaderReflection), &reflector) || FAILED(reflector->GetDesc(&desc)))
      return buffers;
   for (UINT i = 0; i < desc.ConstantBuffers; i++)
   {
      ID3D11ShaderReflectionConstantBuffer* buffer = reflector->GetConstantBufferByIndex(i);
      D3D11_SHADER_BUFFER_DESC buffer_desc = {};
      D3D11_SHADER_INPUT_BIND_DESC binding = {};
      if (!buffer || FAILED(buffer->GetDesc(&buffer_desc)) || buffer_desc.Type != D3D_CT_CBUFFER ||
         FAILED(reflector->GetResourceBindingDescByName(buffer_desc.Name, &binding)))
         continue;
      ConstantBuffer& reflected = buffers.emplace_back(buffer_desc.Name, binding.BindPoint, buffer_desc.Size);
      for (UINT j = 0; j < buffer_desc.Variables; j++)
      {
         ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(j);
         D3D11_SHADER_VARIABLE_DESC variable_desc = {};
         if (variable && SUCCEEDED(variable->GetDesc(&variable_desc)))
            AddVariables<D3D11_SHADER_TYPE_DESC>(variable->GetType(), variable_desc.Name, variable_desc.StartOffset, variable_desc.Size, reflected.variables);
      }
   }
   return buffers;
}
#endif
} // namespace Compiler
} // namespace Shaders
} // namespace Luma
