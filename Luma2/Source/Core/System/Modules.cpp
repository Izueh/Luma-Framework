#include "System/Modules.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <winternl.h>

#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace System {
namespace Modules {
namespace {
HMODULE self = nullptr;
std::atomic<bool> pinned = false;

// Luma's dll, also when SetSelf wasn't called (Tests links Core into its executable)
HMODULE FindSelf()
{
   HMODULE module = nullptr;
   GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&FindSelf), &module);
   return module;
}
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

HMODULE GetSelf()
{
   return self;
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

void* FindExport(HMODULE module, const char* name, bool* waits_for_dll)
{
   if (waits_for_dll)
      *waits_for_dll = false;
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
      // Loading it here could deadlock under the loader lock: the hook waits for the dll's own load
      HMODULE forwarded = GetModuleHandleA(dll.c_str());
      if (!forwarded)
      {
         if (waits_for_dll)
            *waits_for_dll = true;
         return nullptr;
      }
      if (target.starts_with('#'))
         return FindExport(forwarded, MAKEINTRESOURCEA(std::stoi(target.substr(1))), waits_for_dll);
      return FindExport(forwarded, target.c_str(), waits_for_dll);
   }
   return const_cast<uint8_t*>(base + rva);
}

std::string GetName(const void* address)
{
   HMODULE module = nullptr;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
      return "?";
   return Utils::Strings::ToUtf8(GetPath(module).filename());
}

bool IsSystemModule(HMODULE module)
{
   if (!module)
      return false;
   const std::filesystem::path directory = GetPath(module).parent_path();
   wchar_t system_directory[MAX_PATH];
   UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
   if (Utils::Strings::EqualsIgnoreCase(directory.native(), std::wstring_view(system_directory, length)))
      return true;
   // A 32-bit process loads its system dlls from SysWOW64, while GetSystemDirectory says System32
   length = GetSystemWow64DirectoryW(system_directory, MAX_PATH);
   return length != 0 && Utils::Strings::EqualsIgnoreCase(directory.native(), std::wstring_view(system_directory, length));
}

bool IsInSystemModule(const void* address)
{
   HMODULE module = nullptr;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
      return false;
   return IsSystemModule(module);
}

bool IsLoaderLockHeld()
{
   // PEB::LoaderLock, at the same place since Windows XP
#ifdef _WIN64
   constexpr size_t LOADER_LOCK_OFFSET = 0x110;
#else
   constexpr size_t LOADER_LOCK_OFFSET = 0xA0;
#endif
   const auto* peb = reinterpret_cast<const uint8_t*>(NtCurrentTeb()->ProcessEnvironmentBlock);
   const auto* lock = *reinterpret_cast<const RTL_CRITICAL_SECTION* const*>(peb + LOADER_LOCK_OFFSET);
   return lock && lock->OwningThread == reinterpret_cast<HANDLE>(uintptr_t(GetCurrentThreadId()));
}

void Pin()
{
   if (pinned.exchange(true))
      return;
   HMODULE module = nullptr;
   GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&FindSelf), &module);
   Utils::Log::Info("Core: Luma's dll stays loaded until the process exits");
}
} // namespace Modules
} // namespace System
} // namespace Luma
