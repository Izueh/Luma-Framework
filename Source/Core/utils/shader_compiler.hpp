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

#include "system.h"

namespace Shader
{
   static HMODULE d3d_compiler;

   typedef HRESULT(WINAPI* pD3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DPreprocess)(LPCVOID, SIZE_T, LPCSTR, CONST D3D_SHADER_MACRO*, ID3DInclude*, ID3DBlob**, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DReadFileToBlob)(LPCWSTR, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DCompileFromFile)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DWriteBlobToFile)(ID3DBlob*, LPCWSTR, BOOL);
   typedef HRESULT(WINAPI* pD3DReflect)(LPCVOID, SIZE_T, REFIID, void**);
   typedef HRESULT(WINAPI* pD3DGetBlobPart)(LPCVOID, SIZE_T, D3D_BLOB_PART, UINT, ID3DBlob**);
   typedef HRESULT(WINAPI* pD3DStripShader)(LPCVOID, SIZE_T, UINT, ID3DBlob**);

   static pD3DDisassemble d3d_disassemble;
   static pD3DPreprocess d3d_preprocess;
   static pD3DReadFileToBlob d3d_readFileToBlob;
   static pD3DCompileFromFile d3d_compileFromFile;
   static pD3DCompile d3d_compile;
   static pD3DWriteBlobToFile d3d_writeBlobToFile;
   static pD3DReflect d3d_reflect;
   static pD3DGetBlobPart d3d_getBlobPart;
   static pD3DStripShader d3d_stripShader;

   enum class DXBCProgramType : uint16_t
   {
      PixelShader = 0,
      VertexShader = 1,
      GeometryShader = 2,
      HullShader = 3,
      DomainShader = 4,
      ComputeShader = 5,
   };
   struct DXBCHeader
   {
      static constexpr DWORD hash_size = 16;

      char format_name[4]; // 'DXBC'
      uint8_t hash[hash_size]; // Checksum MD5
      uint32_t version; // Seemengly always 1
      uint32_t file_size; // Total size in bytes (including the header)
      uint32_t chunk_count;
      uint32_t chunk_offsets[]; // Array of DWORD offsets in bytes, from the beginning of the object/header
   };
   struct DXBCChunk
   {
      uint32_t type_name;
      uint32_t chunk_size; // Total size of the chunk in bytes, NOT including the type name and size

      uint32_t chunk_data[];
   };
   struct DXBCByteCodeChunk
   {
      uint8_t version_major_and_minor; // E.g. SM4.0/5.1
      uint8_t reserved;
      DXBCProgramType program_type;

      uint32_t chunk_size_dword; // The size is stored in "DWORD" elements and counted the size (this very variable) and program version/type in its count

      uint8_t byte_code[];
   };

   // Only call this once from one thread
   // Needs to be called before calling any shader compiler function
   bool InitShaderCompiler()
   {
      assert(!d3d_compiler);

      LPCWSTR library = L"D3DCompiler_47.dll";
      LPCWSTR library_x32 = L"D3DCompiler_47_x32.dll";

      // We optionally ship with a recent version of the shader compiler, to allow Linux users given they might not have it or it might be outdated/incomplete and fail to compile shaders.
      // We need to use the product version for this as for some reason the file version has different schemes between System32 and other dlls (apparently the other ones are marked for redistribution, however they have the same exact functionality).
      // As of 2025, the ones in System32 have a 6.x file version scheme, while the other ones 10.x, however the product version is 10.x in both.
      // It's not 100% clear this makes sense, as theoretically the product version would be the version of the package they were delivered with, so be it the DX SDK, Visual Studio, Windows, NV drives etc
      // Note: in "DEVELOPMENT" builds this might not exist, as we directly check the "Luma" folder, not the "shaders_path" folder.
      std::filesystem::path embedded_dll_path = System::GetModulePath().parent_path();
      embedded_dll_path /= Globals::MOD_NAME;
#ifdef _WIN64
      embedded_dll_path.append(library);
#else
      embedded_dll_path.append(library_x32);
#endif
      uint64_t embedded_dll_version = 0;
      uint64_t dummy_version;
      if (std::filesystem::is_regular_file(embedded_dll_path))
      {
         System::GetDLLVersion(embedded_dll_path, dummy_version, embedded_dll_version);
      }

      std::filesystem::path system_dll_path = System::GetSystemPath() / library; // Windows redirect automatically to x64 or x32
      uint64_t system_dll_version = 0;
      if (std::filesystem::is_regular_file(system_dll_path))
      {
         System::GetDLLVersion(system_dll_path, dummy_version, system_dll_version);
      }

      // Prefer loading the system one as it's likely already loaded either by ReShade or the game
      if (system_dll_version >= embedded_dll_version)
      {
         d3d_compiler = LoadLibraryW(system_dll_path.c_str());
      }
      else if (embedded_dll_version > 0)
      {
         d3d_compiler = LoadLibraryW(embedded_dll_path.c_str());
      }
      // In case everything failed, fall back on loading the one from the game, assuming there was one (most of the times there isn't, but this shouldn't happen)
      else
      {
         assert(false);

         std::filesystem::path local_dll_path = System::GetModulePath().parent_path();
         local_dll_path.append(library);
         d3d_compiler = LoadLibraryW(local_dll_path.c_str());
      }

      if (!d3d_compiler)
         return false;

      d3d_disassemble = pD3DDisassemble(GetProcAddress(d3d_compiler, "D3DDisassemble"));
      d3d_preprocess = pD3DPreprocess(GetProcAddress(d3d_compiler, "D3DPreprocess"));
      d3d_readFileToBlob = pD3DReadFileToBlob(GetProcAddress(d3d_compiler, "D3DReadFileToBlob"));
      d3d_compileFromFile = pD3DCompileFromFile(GetProcAddress(d3d_compiler, "D3DCompileFromFile"));
      d3d_compile = pD3DCompile(GetProcAddress(d3d_compiler, "D3DCompile"));
      d3d_writeBlobToFile = pD3DWriteBlobToFile(GetProcAddress(d3d_compiler, "D3DWriteBlobToFile"));
      d3d_reflect = pD3DReflect(GetProcAddress(d3d_compiler, "D3DReflect"));
      d3d_getBlobPart = pD3DGetBlobPart(GetProcAddress(d3d_compiler, "D3DGetBlobPart"));
      d3d_stripShader = pD3DStripShader(GetProcAddress(d3d_compiler, "D3DStripShader"));

      return true;
   }

   void UnInitShaderCompiler()
   {
      if (d3d_compiler) // Optional check
      {
         FreeLibrary(d3d_compiler);
         d3d_compiler = nullptr;
      }
   }

   bool dummy_bool;

   // From "dxbc-spirv", developed by Philip Rebohle, MIT license
   Hash::MD5::Digest CalcDXBCHash(const void* data, size_t size)
   {
      constexpr size_t BlockSize = 64u;

      /* Skip initial part of the header including the hash digest */
      size_t offset = offsetof(DXBCHeader, version);

      if (size < offset)
         return Hash::MD5::Digest();

      auto bytes = reinterpret_cast<const unsigned char*>(data) + offset;
      size -= offset;

      /* Compute byte representations of the bit count and a derived
       * number that will be appended to the stream */
      const uint32_t aNum = uint32_t(size) * 8u;
      const uint32_t bNum = (aNum >> 2u) | 1u;

      std::array<uint8_t, sizeof(uint32_t)> a = { };
      std::array<uint8_t, sizeof(uint32_t)> b = { };

      for (uint32_t i = 0u; i < sizeof(uint32_t); i++) {
         a[i] = Math::Bextract(aNum, 8u * i, 8u);
         b[i] = Math::Bextract(bNum, 8u * i, 8u);
      }

      /* Hash remaining header and all chunk data */
      size_t remainder = size % BlockSize;
      size_t paddingSize = BlockSize - remainder;

      Hash::MD5::Hasher hasher = { };
      hasher.update(bytes, size - remainder);

      /* DXBC hashing does not finalize the last block properly, instead
       * padding behaviour depends on the size of the byte stream */
      static const std::array<uint8_t, BlockSize> s_padding = { 0x80u };

      if (remainder >= 56u) {
         /* Append last block and pad to multiple of 64 bytes */
         hasher.update(&bytes[size - remainder], remainder);
         hasher.update(s_padding.data(), paddingSize);

         /* Pad with null block and custom finalizer */
         hasher.update(a.data(), a.size());
         hasher.update(s_padding.data() + a.size(), s_padding.size() - a.size() - b.size());
         hasher.update(b.data(), b.size());
      }
      else {
         /* Append bit count */
         hasher.update(a.data(), a.size());

         /* Append last block */
         if (remainder)
            hasher.update(&bytes[size - remainder], remainder);

         /* Append regular padding sequence */
         hasher.update(s_padding.data(), paddingSize - a.size() - b.size());

         /* Append final magic number */
         hasher.update(b.data(), b.size());
      }

      return hasher.getDigest();
   }
   
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

   std::optional<std::string> DisassembleShaderFXC(const void* data, size_t size)
   {
      std::optional<std::string> result;

      if (d3d_disassemble != nullptr)
      {
         CComPtr<ID3DBlob> out_blob;
         HRESULT hr = d3d_disassemble(
            data,
            size,
            /*D3D_DISASM_ENABLE_COLOR_CODE |*/ D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS | D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING | D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
            nullptr,
            &out_blob);
         if (SUCCEEDED(hr))
         {
            result = { reinterpret_cast<char*>(out_blob->GetBufferPointer()), out_blob->GetBufferSize() };
         }
         else
         {
            std::stringstream s;
            s << "D3DDisassemble failed with HRESULT 0x" << std::hex << std::uppercase << hr;
            reshade::log::message(reshade::log::level::error, s.str().c_str());
         }
      }

      return result;
   }

   HRESULT CreateLibrary(IDxcLibrary** dxc_library)
   {
      //HMODULE dxil_loader = LoadLibraryW(L"dxil.dll");
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
      //HMODULE dxil_loader = LoadLibraryW(L"dxil.dll");
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

   std::optional<std::string> DisassembleShaderDXC(const void* data, size_t size)
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

   std::optional<std::string> DisassembleShader(const void* code, size_t size)
   {
#if DX12
      return DisassembleShaderDXC(code, size);
#else
      return DisassembleShaderFXC(code, size);
#endif
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

   // Returns true if the shader changed (or if we can't compare it).
   // Pass in "shader_name_w" as the full path to avoid needing to set the current directory.
   bool PreprocessShaderFromFile(LPCWSTR file_path, LPCWSTR shader_name_w, LPCSTR shader_target, std::string& preprocessed_code, std::size_t& preprocessed_hash /*= 0*/, CComPtr<ID3DBlob>& uncompiled_code_blob, const std::vector<std::string>& defines = {}, bool& error = dummy_bool, std::string* out_error = nullptr)
   {
      std::vector<D3D_SHADER_MACRO> local_defines;
      FillDefines(defines, local_defines);

      if (shader_target[3] < '6')
      {
         if (d3d_readFileToBlob != nullptr && d3d_preprocess != nullptr)
         {
            auto custom_include = FxcD3DInclude(shader_name_w);
            
            if (SUCCEEDED(d3d_readFileToBlob(file_path, &uncompiled_code_blob)))
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
               HRESULT result = d3d_preprocess(
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
   bool LoadCompiledShaderFromFile(std::vector<uint8_t>& output, LPCWSTR file_path)
   {
      bool file_loaded = false;
      CComPtr<ID3DBlob> out_blob;
      if (d3d_readFileToBlob != nullptr)
      {
         std::wstring file_path_cso = file_path;
         if (file_path_cso.ends_with(L".hlsl"))
         {
            file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5); // strlen(".hlsl")
            file_path_cso += L".cso";
         }
         else if (!file_path_cso.ends_with(L".cso"))
         {
            file_path_cso += L".cso";
         }

         CComPtr<ID3DBlob> out_blob;
         HRESULT result = d3d_readFileToBlob(file_path_cso.c_str(), &out_blob);
         if (SUCCEEDED(result))
         {
            output.assign(
               reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()),
               reinterpret_cast<uint8_t*>(out_blob->GetBufferPointer()) + out_blob->GetBufferSize());
            file_loaded = true;
         }
         // No need for warnings if the file failed loading or didn't exist, that's expected to happen
      }

      return file_loaded;
   }

   void CompileShaderFromFileFXC(std::vector<uint8_t>& output, const CComPtr<ID3DBlob>& optional_uncompiled_code_input, LPCWSTR file_read_path, LPCSTR shader_target, const D3D_SHADER_MACRO* defines = nullptr, bool save_to_disk = false, bool& error = dummy_bool, std::string* out_error = nullptr, LPCWSTR file_write_path = nullptr, LPCSTR func_name = nullptr)
   {
      UINT flags1 = 0;
      constexpr bool force_backwards_compatibility = false; // Expose if needed, so far no games really needed it
      bool needs_backwards_compatibility = shader_target[3] <= '3';
      // Some behaviours this changes:
      // -Allows compiling shader model 2 and 3?
      // -Allows not initializing shader code register, either having undefined behaviour or defaulting them to 0
      // -Allows defining multiple cbuffers in the same slot?
      // -More lenient float/int conversions?
      // -Slightly worse code performance?
      if (needs_backwards_compatibility || (force_backwards_compatibility && (shader_target[3] <= '4' || (shader_target[3] == '5' && shader_target[5] == '0'))))
         flags1 |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY; // /Gec
#if _DEBUG && 0
      flags1 |= D3DCOMPILE_DEBUG; // /Zi
      flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION; // /Od
      if ((flags1 & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY) == 0) // Not mutually compatible
         flags1 |= D3DCOMPILE_ENABLE_STRICTNESS; // /Ges
      flags1 |= D3DCOMPILE_IEEE_STRICTNESS; // /Gis
#else
      flags1 |= D3DCOMPILE_OPTIMIZATION_LEVEL0; // /O3
      //flags1 |= D3DCOMPILE_SKIP_VALIDATION; // Faster to compile? We could do this in publishing mode
      //flags1 |= D3DCOMPILE_SKIP_REFLECTION_DATA; // It removes the reflections data, I don't think we want this
#endif

      if (func_name == nullptr)
         func_name = "main";

      auto custom_include = FxcD3DInclude(file_read_path);

      CComPtr<ID3DBlob> out_blob;
      CComPtr<ID3DBlob> error_blob;
      HRESULT result = E_FAIL; // Fake default error
      if (optional_uncompiled_code_input != nullptr && d3d_compile != nullptr)
      {
#pragma warning(push)
#pragma warning(disable : 4244)
         const std::wstring& shader_name_w_s = file_read_path;
         std::string shader_name_s(shader_name_w_s.length(), ' ');
         std::copy(shader_name_w_s.begin(), shader_name_w_s.end(), shader_name_s.begin());
         LPCSTR shader_name = shader_name_s.c_str();
#pragma warning(pop)
         result = d3d_compile(
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
      if (FAILED(result) && d3d_compileFromFile != nullptr)
      {
         out_blob.Release();
         error_blob.Release();

         result = d3d_compileFromFile(
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

         if (save_to_disk && d3d_writeBlobToFile != nullptr)
         {
            const bool overwrite = true; // Overwrite whatever original or custom shader we previously had there
            std::wstring file_path_cso = (file_write_path && file_write_path[0] != '\0') ? file_write_path : file_read_path;
            if (file_path_cso.ends_with(L".hlsl"))
            {
               file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5); // strlen(".hlsl")
               file_path_cso += L".cso";
            }
            else if (!file_path_cso.ends_with(L".cso"))
            {
               file_path_cso += L".cso";
            }
            HRESULT result2 = d3d_writeBlobToFile(out_blob, file_path_cso.c_str(), overwrite);
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
         if ((flags1 & D3DCOMPILE_OPTIMIZATION_LEVEL2) != 0) // Same as "D3DCOMPILE_OPTIMIZATION_LEVEL0|D3DCOMPILE_OPTIMIZATION_LEVEL3"
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

   void CompileShaderFromFileDXC(std::vector<uint8_t>& output, LPCWSTR file_path, LPCSTR shader_target, const D3D_SHADER_MACRO* defines = nullptr, bool& error = dummy_bool, LPCSTR func_name = nullptr, std::string* out_error = nullptr)
   {
      if (func_name == nullptr)
         func_name = "main";

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

   void CompileShaderFromFile(std::vector<uint8_t>& output, const CComPtr<ID3DBlob>& optional_uncompiled_code_input, LPCWSTR file_path, LPCSTR shader_target, const std::vector<std::string>& defines = {}, bool save_to_disk = false, bool& error = dummy_bool, std::string* out_error = nullptr, LPCWSTR file_write_path = nullptr, LPCSTR func_name = nullptr)
   {
      std::vector<D3D_SHADER_MACRO> local_defines;
      FillDefines(defines, local_defines);

      if (shader_target[3] < '6')
      {
         CompileShaderFromFileFXC(output, optional_uncompiled_code_input, file_path, shader_target, local_defines.data(), save_to_disk, error, out_error, file_write_path, func_name);
         return;
      }
      CompileShaderFromFileDXC(output, file_path, shader_target, local_defines.data(), error, func_name, out_error);
   }
}