#include "utils/modules.hpp"

#include <cstdint>
#include <cstring>

#include "utils/strings.hpp"

namespace Luma {
namespace Utils {
namespace Modules {
namespace {
HMODULE self = nullptr;
} // namespace

void SetSelf(HMODULE module)
{
   self = module;
}

std::filesystem::path GetPath(HMODULE module)
{
   std::wstring path(MAX_PATH, L'\0');
   while (true)
   {
      const DWORD length = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
      if (length == 0)
         return {};
      if (length < path.size())
      {
         path.resize(length);
         return path;
      }
      path.resize(path.size() * 2);
   }
}

std::filesystem::path SelfDirectory()
{
   return GetPath(self).parent_path();
}

std::filesystem::path SystemPath(std::wstring_view name)
{
   wchar_t system_directory[MAX_PATH];
   const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
   return std::filesystem::path(std::wstring_view(system_directory, length)) / name;
}

HMODULE FindSystemModule(std::wstring_view name)
{
   return GetModuleHandleW(SystemPath(name).c_str());
}

void* FindExport(HMODULE module, const char* name)
{
   if (!module || !name)
      return nullptr;
   const auto* base = reinterpret_cast<const uint8_t*>(module);
   const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
   if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return nullptr;
   const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
   if (nt->Signature != IMAGE_NT_SIGNATURE)
      return nullptr;
   const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
   if (!directory.VirtualAddress || !directory.Size)
      return nullptr;
   const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
   const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);

   DWORD index = DWORD(-1);
   if (IS_INTRESOURCE(name))
      index = DWORD(reinterpret_cast<uintptr_t>(name)) - exports->Base;
   else
   {
      const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
      const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
      DWORD low = 0;
      DWORD high = exports->NumberOfNames;
      while (low < high)
      {
         const DWORD middle = (low + high) / 2;
         const int order = std::strcmp(name, reinterpret_cast<const char*>(base + names[middle]));
         if (order == 0)
         {
            index = ordinals[middle];
            break;
         }
         if (order < 0)
            high = middle;
         else
            low = middle + 1;
      }
   }
   if (index >= exports->NumberOfFunctions || !functions[index])
      return nullptr;

   const DWORD rva = functions[index];
   // A forwarder: the address is a "Dll.Export" (or "Dll.#Ordinal") string inside the export directory
   if (rva >= directory.VirtualAddress && rva < directory.VirtualAddress + directory.Size)
   {
      const std::string_view forwarder = reinterpret_cast<const char*>(base + rva);
      const size_t dot = forwarder.rfind('.');
      if (dot == std::string_view::npos)
         return nullptr;
      const std::string dll = std::string(forwarder.substr(0, dot)) + ".dll";
      const std::string target = std::string(forwarder.substr(dot + 1));
      HMODULE forwarded = LoadLibraryA(dll.c_str());
      if (!forwarded)
         return nullptr;
      if (target.starts_with('#'))
         return FindExport(forwarded, MAKEINTRESOURCEA(std::stoi(target.substr(1))));
      return FindExport(forwarded, target.c_str());
   }
   return const_cast<uint8_t*>(base + rva);
}

std::string GetName(const void* address)
{
   HMODULE module = nullptr;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
      return "?";
   return Strings::ToUtf8(GetPath(module).filename());
}
} // namespace Modules
} // namespace Utils
} // namespace Luma
