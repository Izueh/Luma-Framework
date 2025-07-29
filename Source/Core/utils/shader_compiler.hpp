#pragma once

#include <atlbase.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <map>

#include <include/reshade.hpp>

namespace Shader
{
   static std::mutex s_mutex_shader_compiler;

   bool dummy_bool;
   
   // TODO: optimize
   std::optional<std::string> ReadTextFile(const std::filesystem::path& path, bool force_value = false)
   {
      std::vector<uint8_t> data;
      std::optional<std::string> result = std::nullopt;
      if (force_value) result = "";
      std::ifstream file(path, std::ios::binary);
      if (!file) return result;
      file.seekg(0, std::ios::end);
      const size_t file_size = file.tellg();
      if (file_size == 0) return result;

      data.resize(file_size);
      file.seekg(0, std::ios::beg).read(reinterpret_cast<char*>(data.data()), file_size);
      result = std::string(reinterpret_cast<const char*>(data.data()), file_size);
      return result;
   }

   constexpr bool custom_include_handler = true;

   // Custom D3DInclude that supports nested relative imports
   // From ShortFuse
   class FxcD3DInclude : public ID3DInclude
   {
   public:
      LPCWSTR initial_file;
      explicit FxcD3DInclude(LPCWSTR initial_file)
      {
         this->initial_file = initial_file;
      };

      // Don't use map in case file contents are identical
      std::vector<std::pair<std::string, std::filesystem::path>> file_paths;
      std::map<std::filesystem::path, std::string> file_contents;

      HRESULT __stdcall Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
      {
         std::filesystem::path new_path;
         if (pParentData != nullptr)
         {
            std::string parent_data = static_cast<const char*>(pParentData);
            for (auto pair = file_paths.rbegin(); pair != file_paths.rend(); ++pair)
            {
               if (pair->first == parent_data)
               {
                  new_path = pair->second.parent_path();
                  break;
               }
            }
         }
         if (new_path.empty())
         {
            new_path = initial_file;
            new_path = new_path.parent_path();
         }

         new_path /= pFileName;
         new_path = new_path.lexically_normal();

         *ppData = nullptr;
         *pBytes = 0;

         try
         {
            std::string output;
            if (auto pair = file_contents.find(new_path); pair != file_contents.end())
            {
               output = pair->second;
            }
            else
            {
               output = ReadTextFile(new_path, true).value();
            }
            file_paths.emplace_back(output, new_path);

            *ppData = _strdup(output.c_str());
            *pBytes = static_cast<UINT>(output.size());
         }
         catch (...)
         {
            {
               std::stringstream s;
               s << "FxcD3DInclude::Open(Failed to open";
               s << pFileName;
               s << ", type: " << IncludeType;
               s << ", parent: " << pParentData;
               s << ")";
            }
            return -1; // Error
         }

         return S_OK;
      }

      HRESULT __stdcall Close(LPCVOID pData) override
      {
         if (pData != nullptr)
         {
            std::string data = static_cast<const char*>(pData);
            for (auto pair = file_paths.rbegin(); pair != file_paths.rend(); ++pair)
            {
               if (pair->first == data)
               {
                  file_paths.erase(std::next(pair).base());
                  break;
               }
            }
         }

         free(const_cast<void*>(pData));
         return S_OK;
      }
   };

   std::optional<std::string> DisassembleShaderFXC(void* data, size_t size, LPCWSTR library = L"D3DCompiler_47.dll")
   {
      std::optional<std::string> result;

      // TODO: unify this with "CompileShaderFromFileFXC()", as it loads the same dll.
      static std::unordered_map<LPCWSTR, pD3DDisassemble> d3d_disassemble;
      {
         const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
         static std::unordered_map<LPCWSTR, HMODULE> d3d_compiler;
         if (d3d_compiler[library] == nullptr)
         {
            d3d_compiler[library] = LoadLibraryW(library);
         }
         if (d3d_compiler[library] != nullptr && d3d_disassemble[library] == nullptr)
         {
            d3d_disassemble[library] = pD3DDisassemble(GetProcAddress(d3d_compiler[library], "D3DDisassemble"));
         }
      }

      if (d3d_disassemble[library] != nullptr)
      {
         CComPtr<ID3DBlob> out_blob;
         if (SUCCEEDED(d3d_disassemble[library](
            data,
            size,
            D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING | D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
            nullptr,
            &out_blob)))
         {
            result = { reinterpret_cast<char*>(out_blob->GetBufferPointer()), out_blob->GetBufferSize() };
         }
      }

#if 0  // Not much point in unloading the library, we'd need to keep "s_mutex_shader_compiler" locked the whole time.
      FreeLibrary(d3d_compiler[library]);
#endif

      return result;
   }

   HRESULT CreateLibrary(IDxcLibrary** dxc_library)
   {
      const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
      // HMODULE dxil_loader = LoadLibraryW(L"dxil.dll");
      HMODULE dx_compiler = LoadLibraryW(L"dxcompiler.dll");
      if (dx_compiler == nullptr)
      {
         reshade::log::message(reshade::log::level::error, "dxcompiler.dll not loaded");
         return -1;
      }
      auto dxc_create_instance = DxcCreateInstanceProc(GetProcAddress(dx_compiler, "DxcCreateInstance"));
      if (dxc_create_instance == nullptr) return -1;
      return dxc_create_instance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(dxc_library));
   }

   HRESULT CreateCompiler(IDxcCompiler** dxc_compiler)
   {
      const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
      // HMODULE dxil_loader = LoadLibraryW(L"dxil.dll");
      HMODULE dx_compiler = LoadLibraryW(L"dxcompiler.dll");
      if (dx_compiler == nullptr)
      {
         reshade::log::message(reshade::log::level::error, "dxcompiler.dll not loaded");
         return -1;
      }
      auto dxc_create_instance = DxcCreateInstanceProc(GetProcAddress(dx_compiler, "DxcCreateInstance"));
      if (dxc_create_instance == nullptr) return -1;
      return dxc_create_instance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(dxc_compiler));
   }

   std::optional<std::string> DisassembleShaderDXC(void* data, size_t size)
   {
      CComPtr<IDxcLibrary> library;
      CComPtr<IDxcCompiler> compiler;
      CComPtr<IDxcBlobEncoding> source;
      CComPtr<IDxcBlobEncoding> disassembly_text;
      CComPtr<ID3DBlob> disassembly;

      std::optional<std::string> result;

      if (FAILED(CreateLibrary(&library))) return result;
      if (FAILED(library->CreateBlobWithEncodingFromPinned(data, size, CP_ACP, &source))) return result;
      if (FAILED(CreateCompiler(&compiler))) return result;
      if (FAILED(compiler->Disassemble(source, &disassembly_text))) return result;
      if (FAILED(disassembly_text.QueryInterface(&disassembly))) return result;

      result = { reinterpret_cast<char*>(disassembly->GetBufferPointer()), disassembly->GetBufferSize() };

      return result;
   }

   std::optional<std::string> DisassembleShader(void* code, size_t size)
   {
      auto result = DisassembleShaderFXC(code, size);
      if (!result.has_value())
      {
         result = DisassembleShaderDXC(code, size);
      }
      return result;
   }

   void FillDefines(const std::vector<std::string>& in_defines, std::vector<D3D_SHADER_MACRO>& out_defines)
   {
      for (int i = 0; i < in_defines.size() && in_defines.size() > 1; i += 2)
      {
         if (!in_defines[i].empty() && !in_defines[i + 1].empty())
         {
            out_defines.push_back({ in_defines[i].c_str(), in_defines[i + 1].c_str() });
         }
      }
      // It needs to be null terminated
      if (out_defines.size() > 0)
      {
         out_defines.push_back({ nullptr, nullptr });
      }
   }

   static std::unordered_map<LPCWSTR, HMODULE> d3d_compiler;

   // Returns true if the shader changed (or if we can't compare it).
   // Pass in "shader_name_w" as the full path to avoid needing to set the current directory.
   bool PreprocessShaderFromFile(LPCWSTR file_path, LPCWSTR shader_name_w, LPCSTR shader_target, std::string& preprocessed_code, std::size_t& preprocessed_hash /*= 0*/, CComPtr<ID3DBlob>& uncompiled_code_blob, const std::vector<std::string>& defines = {}, bool& error = dummy_bool, std::string* out_error = nullptr, LPCWSTR fxc_library = L"D3DCompiler_47.dll")
   {
      std::vector<D3D_SHADER_MACRO> local_defines;
      FillDefines(defines, local_defines);

      if (shader_target[3] < '6')
      {
         typedef HRESULT(WINAPI* pD3DReadFileToBlob)(LPCWSTR, ID3DBlob**);
         typedef HRESULT(WINAPI* pD3DPreprocess)(LPCVOID, SIZE_T, LPCSTR, CONST D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);
         static std::unordered_map<LPCWSTR, pD3DReadFileToBlob> d3d_readFileToBlob;
         static std::unordered_map<LPCWSTR, pD3DPreprocess> d3d_preprocess;
         {
            const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
            if (d3d_compiler[fxc_library] == nullptr)
            {
               d3d_compiler[fxc_library] = LoadLibraryW(fxc_library);
            }
            if (d3d_compiler[fxc_library] != nullptr && d3d_readFileToBlob[fxc_library] == nullptr)
            {
               d3d_readFileToBlob[fxc_library] = pD3DReadFileToBlob(GetProcAddress(d3d_compiler[fxc_library], "D3DReadFileToBlob"));
               d3d_preprocess[fxc_library] = pD3DPreprocess(GetProcAddress(d3d_compiler[fxc_library], "D3DPreprocess"));
            }
         }

         if (d3d_readFileToBlob[fxc_library] != nullptr && d3d_preprocess[fxc_library] != nullptr)
         {
            auto custom_include = FxcD3DInclude(shader_name_w);
            
            if (SUCCEEDED(d3d_readFileToBlob[fxc_library](file_path, &uncompiled_code_blob)))
            {
#pragma warning(push)
#pragma warning(disable : 4244)
               const std::wstring& shader_name_w_s = shader_name_w;
               std::string shader_name_s(shader_name_w_s.length(), ' ');
               std::copy(shader_name_w_s.begin(), shader_name_w_s.end(), shader_name_s.begin());
               LPCSTR shader_name = shader_name_s.c_str();
#pragma warning(pop)
               CComPtr<ID3DBlob> preprocessed_blob;
               CComPtr<ID3DBlob> error_blob;
               HRESULT result = d3d_preprocess[fxc_library](
                  uncompiled_code_blob->GetBufferPointer(),
                  uncompiled_code_blob->GetBufferSize(),
                  shader_name,
                  local_defines.data(),
                  custom_include_handler ? &custom_include : D3D_COMPILE_STANDARD_FILE_INCLUDE,
                  &preprocessed_blob,
                  &error_blob);
               error = FAILED(result);
               if (out_error != nullptr && error_blob != nullptr)
               {
                  out_error->assign(reinterpret_cast<char*>(error_blob->GetBufferPointer()));
               }
               if (SUCCEEDED(result) && preprocessed_blob != nullptr)
               {
                  preprocessed_code.assign(reinterpret_cast<char*>(preprocessed_blob->GetBufferPointer()));
                  // TODO: there's possibly a more optimized way of finding the blob's hash
                  std::size_t new_preprocessed_hash = std::hash<std::string>{}(preprocessed_code);
#if _DEBUG // Hacky: in debug mode, always add 1 to the shader hash, so we force it to recompile between Release and Debug builds, given they use different flags (this isn't mandatory, we could rely on devs doing that manually)
                  new_preprocessed_hash++;
#endif
                  if (preprocessed_hash == new_preprocessed_hash)
                  {
                     return false;
                  }
                  preprocessed_hash = new_preprocessed_hash;
               }
            }
         }
      }
      return true;
   }

   // Note: you can pass in an hlsl or cso path or a path without a format, ".cso" will always be added at the end
   bool LoadCompiledShaderFromFile(std::vector<uint8_t>& output, LPCWSTR file_path, LPCWSTR library = L"D3DCompiler_47.dll")
   {
      typedef HRESULT(WINAPI* pD3DReadFileToBlob)(LPCWSTR, ID3DBlob**);
      static std::unordered_map<LPCWSTR, pD3DReadFileToBlob> d3d_readFileToBlob;
      {
         const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
         if (d3d_compiler[library] == nullptr)
         {
            d3d_compiler[library] = LoadLibraryW(library);
         }
         if (d3d_compiler[library] != nullptr && d3d_readFileToBlob[library] == nullptr)
         {
            d3d_readFileToBlob[library] = pD3DReadFileToBlob(GetProcAddress(d3d_compiler[library], "D3DReadFileToBlob"));
         }
      }

      bool file_loaded = false;
      CComPtr<ID3DBlob> out_blob;
      if (d3d_readFileToBlob[library] != nullptr)
      {
         std::wstring file_path_cso = file_path;
         if (file_path_cso.ends_with(L".hlsl"))
         {
            file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5);
            file_path_cso += L".cso";
         }
         else if (!file_path_cso.ends_with(L".cso"))
         {
            file_path_cso += L".cso";
         }

         CComPtr<ID3DBlob> out_blob;
         HRESULT result = d3d_readFileToBlob[library](file_path_cso.c_str(), &out_blob);
         if (SUCCEEDED(result))
         {
            output.assign(
               reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()),
               reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()) + out_blob->GetBufferSize());
            file_loaded = true;
         }
         // No need for warnings if the file failed loading or didn't exist, that's expected to happen
      }

#if 0  // Not much point in unloading the library, we'd need to keep "s_mutex_shader_compiler" locked the whole time.
      FreeLibrary(d3d_compiler[library]);
#endif

      return file_loaded;
   }

   void CompileShaderFromFileFXC(std::vector<uint8_t>& output, const CComPtr<ID3DBlob>& optional_uncompiled_code_input, LPCWSTR file_read_path, LPCSTR shader_target, const D3D_SHADER_MACRO* defines = nullptr, bool save_to_disk = false, bool& error = dummy_bool, std::string* out_error = nullptr, LPCWSTR file_write_path = nullptr, LPCSTR func_name = "main", LPCWSTR library = L"D3DCompiler_47.dll")
   {
      typedef HRESULT(WINAPI* pD3DCompileFromFile)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
      typedef HRESULT(WINAPI* pD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
      typedef HRESULT(WINAPI* pD3DWriteBlobToFile)(ID3DBlob*, LPCWSTR, BOOL);
      static std::unordered_map<LPCWSTR, pD3DCompileFromFile> d3d_compilefromfile;
      static std::unordered_map<LPCWSTR, pD3DCompile> d3d_compile;
      static std::unordered_map<LPCWSTR, pD3DWriteBlobToFile> d3d_writeBlobToFile;
      {
         const std::lock_guard<std::mutex> lock(s_mutex_shader_compiler);
         if (d3d_compiler[library] == nullptr)
         {
            d3d_compiler[library] = LoadLibraryW(library);
         }
         if (d3d_compiler[library] != nullptr && d3d_compilefromfile[library] == nullptr)
         {
            d3d_compilefromfile[library] = pD3DCompileFromFile(GetProcAddress(d3d_compiler[library], "D3DCompileFromFile"));
            d3d_compile[library] = pD3DCompile(GetProcAddress(d3d_compiler[library], "D3DCompile"));
            d3d_writeBlobToFile[library] = pD3DWriteBlobToFile(GetProcAddress(d3d_compiler[library], "D3DWriteBlobToFile"));
         }
      }

      UINT flags1 = 0;
      if (shader_target[3] <= '4' || (shader_target[3] == '5' && shader_target[5] == '0'))
         flags1 |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY; // /Gec
#if _DEBUG
      flags1 |= D3DCOMPILE_DEBUG; // /Zi
      flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION; // /Od
      if ((flags1 & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY) == 0) // Not mutually compatible
         flags1 |= D3DCOMPILE_ENABLE_STRICTNESS; // /Ges
      flags1 |= D3DCOMPILE_IEEE_STRICTNESS; // /Gis
#else
      flags1 |= D3DCOMPILE_OPTIMIZATION_LEVEL3; // /O3
#endif

      auto custom_include = FxcD3DInclude(file_read_path);

      CComPtr<ID3DBlob> out_blob;
      CComPtr<ID3DBlob> error_blob;
      HRESULT result = E_FAIL; // Fake default error
      if (optional_uncompiled_code_input != nullptr && d3d_compile[library] != nullptr)
      {
#pragma warning(push)
#pragma warning(disable : 4244)
         const std::wstring& shader_name_w_s = file_read_path;
         std::string shader_name_s(shader_name_w_s.length(), ' ');
         std::copy(shader_name_w_s.begin(), shader_name_w_s.end(), shader_name_s.begin());
         LPCSTR shader_name = shader_name_s.c_str();
#pragma warning(pop)
         result = d3d_compile[library](
            optional_uncompiled_code_input->GetBufferPointer(),
            optional_uncompiled_code_input->GetBufferSize(),
            shader_name,
            defines,
            custom_include_handler ? &custom_include : D3D_COMPILE_STANDARD_FILE_INCLUDE,
            func_name,
            shader_target,
            flags1,
            0,
            &out_blob,
            &error_blob);
      }
      if (FAILED(result) && d3d_compilefromfile[library] != nullptr)
      {
         result = d3d_compilefromfile[library](
            file_read_path,
            defines,
            custom_include_handler ? &custom_include : D3D_COMPILE_STANDARD_FILE_INCLUDE,
            func_name,
            shader_target,
            flags1,
            0,
            &out_blob,
            &error_blob);
      }

      if (SUCCEEDED(result))
      {
         output.assign(
            reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()),
            reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()) + out_blob->GetBufferSize());

         if (save_to_disk && d3d_writeBlobToFile[library] != nullptr)
         {
            const bool overwrite = true; // Overwrite whatever original or custom shader we previously had there
            std::wstring file_path_cso = (file_write_path && file_write_path[0] != '\0') ? file_write_path : file_read_path;
            if (file_path_cso.ends_with(L".hlsl"))
            {
               file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5);
               file_path_cso += L".cso";
            }
            else if (!file_path_cso.ends_with(L".cso"))
            {
               file_path_cso += L".cso";
            }
            HRESULT result2 = d3d_writeBlobToFile[library](out_blob, file_path_cso.c_str(), overwrite);
            assert(SUCCEEDED(result2));
         }
      }

      bool failed = FAILED(result);
      error = failed;
      bool error_or_warning = failed || error_blob != nullptr;
      if (error_or_warning)
      {
         std::stringstream s;
         if (failed)
         {
            s << "CompileShaderFromFileFXC(Compilation failed";
         }
         else
         {
            s << "CompileShaderFromFileFXC(Compilation warning";
         }
         if (error_blob != nullptr)
         {
            auto* error = reinterpret_cast<uint8_t*>(error_blob->GetBufferPointer());
            s << ": " << error;
            if (error && out_error != nullptr)
            {
               out_error->assign((char*)error);
            }
         }
         else if (out_error != nullptr)
         {
            *out_error = "Unknown Error";
         }
         s << ")";
         reshade::log::message(failed ? reshade::log::level::error : reshade::log::level::warning, s.str().c_str());
      }
      else if (out_error != nullptr)
      {
         out_error->clear();
      }

#if 0  // Not much point in unloading the library, we'd need to keep "s_mutex_shader_compiler" locked the whole time.
      FreeLibrary(d3d_compiler[library]);
#endif
   }

#define IFR(x)                \
  {                           \
    const HRESULT __hr = (x); \
    if (FAILED(__hr))         \
      return __hr;            \
  }

#define IFT(x)                \
  {                           \
    const HRESULT __hr = (x); \
    if (FAILED(__hr))         \
      throw(__hr);            \
  }

   HRESULT CompileFromBlob(
      IDxcBlobEncoding* source,
      LPCWSTR source_name,
      const D3D_SHADER_MACRO* defines,
      IDxcIncludeHandler* include,
      LPCSTR entrypoint,
      LPCSTR target,
      UINT flags1,
      UINT flags2,
      ID3DBlob** code,
      ID3DBlob** error_messages)
   {
      CComPtr<IDxcCompiler> compiler;
      CComPtr<IDxcOperationResult> operation_result;
      HRESULT hr;

      // Upconvert legacy targets
      char parsed_target[7] = "?s_6_0";
      parsed_target[6] = 0;
      if (target[3] < '6')
      {
         parsed_target[0] = target[0];
         target = parsed_target;
      }

      try
      {
         const CA2W entrypoint_wide(entrypoint, CP_UTF8);
         const CA2W target_profile_wide(target, CP_UTF8);
         std::vector<std::wstring> define_values;
         std::vector<DxcDefine> new_defines;
         if (defines != nullptr)
         {
            CONST D3D_SHADER_MACRO* cursor = defines;

            // Convert to UTF-16.
            while (cursor != nullptr && cursor->Name != nullptr)
            {
               define_values.emplace_back(CA2W(cursor->Name, CP_UTF8));
               if (cursor->Definition != nullptr)
               {
                  define_values.emplace_back(
                     CA2W(cursor->Definition, CP_UTF8));
               }
               else
               {
                  define_values.emplace_back(/* empty */);
               }
               ++cursor;
            }

            // Build up array.
            cursor = defines;
            size_t i = 0;
            while (cursor->Name != nullptr)
            {
               new_defines.push_back(
                  DxcDefine{ define_values[i++].c_str(), define_values[i++].c_str() });
               ++cursor;
            }
         }

         std::vector<LPCWSTR> arguments;
         if ((flags1 & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY) != 0) arguments.push_back(L"/Gec");
         // /Ges Not implemented:
         // if(flags1 & D3DCOMPILE_ENABLE_STRICTNESS) arguments.push_back(L"/Ges");
         if ((flags1 & D3DCOMPILE_IEEE_STRICTNESS) != 0) arguments.push_back(L"/Gis");
         if ((flags1 & D3DCOMPILE_OPTIMIZATION_LEVEL2) != 0)
         {
            switch (flags1 & D3DCOMPILE_OPTIMIZATION_LEVEL2)
            {
            case D3DCOMPILE_OPTIMIZATION_LEVEL0:
            arguments.push_back(L"/O0");
            break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL2:
            arguments.push_back(L"/O2");
            break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL3:
            arguments.push_back(L"/O3");
            break;
            }
         }
         // Currently, /Od turns off too many optimization passes, causing incorrect
         // DXIL to be generated. Re-enable once /Od is implemented properly:
         // if(flags1 & D3DCOMPILE_SKIP_OPTIMIZATION) arguments.push_back(L"/Od");
         if ((flags1 & D3DCOMPILE_DEBUG) != 0) arguments.push_back(L"/Zi");
         if ((flags1 & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR) != 0) arguments.push_back(L"/Zpr");
         if ((flags1 & D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR) != 0) arguments.push_back(L"/Zpc");
         if ((flags1 & D3DCOMPILE_AVOID_FLOW_CONTROL) != 0) arguments.push_back(L"/Gfa");
         if ((flags1 & D3DCOMPILE_PREFER_FLOW_CONTROL) != 0) arguments.push_back(L"/Gfp");
         // We don't implement this:
         // if(flags1 & D3DCOMPILE_PARTIAL_PRECISION) arguments.push_back(L"/Gpp");
         if ((flags1 & D3DCOMPILE_RESOURCES_MAY_ALIAS) != 0) arguments.push_back(L"/res_may_alias");
         arguments.push_back(L"/HV");
         arguments.push_back(L"2021");

         IFR(CreateCompiler(&compiler));
         IFR(compiler->Compile(
            source,
            source_name,
            entrypoint_wide,
            target_profile_wide,
            arguments.data(),
            (UINT)arguments.size(),
            new_defines.data(),
            (UINT)new_defines.size(),
            include,
            &operation_result));
      }
      catch (const std::bad_alloc&)
      {
         return E_OUTOFMEMORY;
      }
      catch (const CAtlException& err)
      {
         return err.m_hr;
      }

      operation_result->GetStatus(&hr);
      if (SUCCEEDED(hr))
      {
         return operation_result->GetResult(reinterpret_cast<IDxcBlob**>(code));
      }
      if (error_messages != nullptr)
      {
         operation_result->GetErrorBuffer(reinterpret_cast<IDxcBlobEncoding**>(error_messages));
      }
      return hr;
   }

   HRESULT WINAPI BridgeD3DCompileFromFile(
      LPCWSTR file_name,
      const D3D_SHADER_MACRO* defines,
      ID3DInclude* include,
      LPCSTR entrypoint,
      LPCSTR target,
      UINT flags1,
      UINT flags2,
      ID3DBlob** code,
      ID3DBlob** error_messages)
   {
      CComPtr<IDxcLibrary> library;
      CComPtr<IDxcBlobEncoding> source;
      CComPtr<IDxcIncludeHandler> include_handler;

      *code = nullptr;
      if (error_messages != nullptr)
      {
         *error_messages = nullptr;
      }

      HRESULT hr;
      hr = CreateLibrary(&library);
      if (FAILED(hr)) return hr;
      hr = library->CreateBlobFromFile(file_name, nullptr, &source);
      if (FAILED(hr)) return hr;

      // Until we actually wrap the include handler, fail if there's a user-supplied
      // handler.
      if (D3D_COMPILE_STANDARD_FILE_INCLUDE == include)
      {
         IFT(library->CreateIncludeHandler(&include_handler));
      }
      else if (include != nullptr)
      {
         return E_INVALIDARG;
      }

      return CompileFromBlob(source, file_name, defines, include_handler, entrypoint, target, flags1, flags2, code, error_messages);
   }

   void CompileShaderFromFileDXC(std::vector<uint8_t>& output, LPCWSTR file_path, LPCSTR shader_target, const D3D_SHADER_MACRO* defines = nullptr, bool& error = dummy_bool, LPCSTR func_name = "main", std::string* out_error = nullptr)
   {
      CComPtr<ID3DBlob> out_blob;
      CComPtr<ID3DBlob> error_blob;
      // TODO: add optional input (code) blob here too
      // TODO: add optional serialization to disk here too
      HRESULT result = BridgeD3DCompileFromFile(
         file_path,
         defines,
         D3D_COMPILE_STANDARD_FILE_INCLUDE,
         func_name,
         shader_target,
         0,
         0,
         &out_blob,
         &error_blob);
      if (SUCCEEDED(result))
      {
         output.assign(
            reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()),
            reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()) + out_blob->GetBufferSize());
      }

      bool failed = FAILED(result);
      error = failed;
      bool error_or_warning = failed || error_blob != nullptr;
      if (error_or_warning)
      {
         std::stringstream s;
         if (failed)
         {
            s << "CompileShaderFromFileDXC(Compilation failed";
         }
         else
         {
            s << "CompileShaderFromFileDXC(Compilation warning";
         }
         if (error_blob != nullptr)
         {
            auto* error = reinterpret_cast<uint8_t*>(error_blob->GetBufferPointer());
            s << ": " << error;
            if (error && out_error != nullptr)
            {
               out_error->assign((char*)error);
            }
         }
         else if (out_error != nullptr)
         {
            *out_error = "Unknown Error";
         }
         s << ")";
         reshade::log::message(failed ? reshade::log::level::error : reshade::log::level::warning, s.str().c_str());
      }
      else if (out_error != nullptr)
      {
         out_error->clear();
      }
   }

   void CompileShaderFromFile(std::vector<uint8_t>& output, const CComPtr<ID3DBlob>& optional_uncompiled_code_input, LPCWSTR file_path, LPCSTR shader_target, const std::vector<std::string>& defines = {}, bool save_to_disk = false, bool& error = dummy_bool, std::string* out_error = nullptr, LPCWSTR file_write_path = nullptr, LPCSTR func_name = "main", LPCWSTR fxc_library = L"D3DCompiler_47.dll")
   {
      std::vector<D3D_SHADER_MACRO> local_defines;
      FillDefines(defines, local_defines);

      if (shader_target[3] < '6')
      {
         CompileShaderFromFileFXC(output, optional_uncompiled_code_input, file_path, shader_target, local_defines.data(), save_to_disk, error, out_error, file_write_path, func_name, fxc_library);
         return;
      }
      CompileShaderFromFileDXC(output, file_path, shader_target, local_defines.data(), error, func_name, out_error);
   }
}