#include "system.h"

#include "assert.h"

// For "GetFileVersionInfoSize" etc
#pragma comment(lib, "Version.lib")

namespace System
{
   std::filesystem::path GetSystemPath()
   {
      WCHAR buf[4096];
      GetSystemDirectoryW(buf, ARRAYSIZE(buf));
      return buf;
   }

   std::filesystem::path GetModulePath(HMODULE hModule)
   {
      std::vector<wchar_t> path(32768); // Maximum path length in Windows, there's no define for it, the old one is "MAX_PATH"
      DWORD length = GetModuleFileNameW(hModule, path.data(), (DWORD)path.size());
      if (length == 0 || length >= path.size()) // This should never happen unless we passed in an invalid module
      {
         assert(false);
         return {};
      }

      std::filesystem::path exe_path = std::wstring(path.data());
      return exe_path;
   }

   std::string GetProcessExecutableName()
   {
      std::string exe_name = GetModulePath().filename().string();
      return exe_name;
   }

   bool GetDLLVersion(const std::filesystem::path& file_path, uint64_t& file_version, uint64_t& product_version)
   {
      file_version = 0;
      product_version = 0;

      DWORD verHandle = 0;
      DWORD verSize = GetFileVersionInfoSize(file_path.c_str(), &verHandle);
      if (verSize != NULL)
      {
         LPSTR verData = new char[verSize];
         if (GetFileVersionInfo(file_path.c_str(), verHandle, verSize, verData))
         {
            LPBYTE lpBuffer = NULL;
            UINT size = 0;
            if (VerQueryValue(verData, L"\\", (VOID FAR * FAR*)&lpBuffer, &size))
            {
               if (size != 0 && lpBuffer != nullptr)
               {
                  VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
                  if (verInfo->dwSignature == 0xfeef04bd)
                  {
                     // Combine into a single 64-bit value: v1.v2.v3.v4. This can 
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionMS >> 16) & 0xFFFF) << 48; // v1
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionMS >> 0) & 0xFFFF) << 32;  // v2
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionLS >> 16) & 0xFFFF) << 16; // v3
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionLS >> 0) & 0xFFFF);        // v4

                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionMS >> 16) & 0xFFFF) << 48; // v1
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionMS >> 0) & 0xFFFF) << 32;  // v2
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionLS >> 16) & 0xFFFF) << 16; // v3
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionLS >> 0) & 0xFFFF);        // v4

                     delete[] verData;
                     return true;
                  }
               }
            }
         }
         delete[] verData;
      }
      return false;
   }

   bool CopyToClipboard(const std::string& text)
   {
#ifdef WIN32
      // Convert UTF-8 to UTF-16
      int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
      if (wideSize <= 0) return false;

      HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideSize * sizeof(wchar_t));
      if (!hMem) return false;

      wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
      if (!wstr)
      {
         GlobalFree(hMem);
         return false;
      }

      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wideSize);
      GlobalUnlock(hMem);

      if (!OpenClipboard(nullptr))
      {
         GlobalFree(hMem);
         return false;
      }

      EmptyClipboard();
      SetClipboardData(CF_UNICODETEXT, hMem); // Windows owns the memory after this
      CloseClipboard();

      return true;
#else
      return false;
#endif
   }

   void OpenExplorerToFile(const std::filesystem::path& file_path)
   {
#ifdef WIN32
      PIDLIST_ABSOLUTE pidl = nullptr;
      HRESULT hr = SHParseDisplayName(file_path.wstring().c_str(), nullptr, &pidl, 0, nullptr);
      if (SUCCEEDED(hr) && (pidl != nullptr)) {
         SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
         CoTaskMemFree(pidl);
      }
#else
      std::string command = "xdg-open " + file_path.parent_path().string();
      std::system(command.c_str());
#endif
   }

   bool CanAllocate(size_t bytes)
   {
      SYSTEM_INFO sysInfo;
      GetSystemInfo(&sysInfo);

      MEMORY_BASIC_INFORMATION mbi = {};
      BYTE* addr = (BYTE*)sysInfo.lpMinimumApplicationAddress;
      BYTE* max_addr = (BYTE*)sysInfo.lpMaximumApplicationAddress;

      while (addr < max_addr)
      {
         if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
            break;

         if (mbi.State == MEM_FREE && mbi.RegionSize >= bytes)
            return true;

         addr += mbi.RegionSize;
      }
      return false;
   }

   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<BytePattern>& pattern, bool stop_at_first)
   {
      if (base == nullptr || pattern.size() == 0 || pattern.size() > size)
         return {};

      std::vector<std::byte*> matches;
      for (size_t i = 0; i <= size - pattern.size(); ++i)
      {
         bool found = true;
         for (size_t j = 0; j < pattern.size(); ++j)
         {
            if (!pattern[j].wildcard && base[i + j] != pattern[j].value)
            {
               found = false;
               break;
            }
         }
         if (found)
         {
            matches.push_back(const_cast<std::byte*>(base + i));
            if (stop_at_first) break;
         }
      }
      return matches;
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::byte* pattern, size_t pattern_size, bool stop_at_first)
   {
      if (pattern == nullptr || base == nullptr || pattern_size == 0 || pattern_size > size)
         return {};

      std::vector<std::byte*> matches;
      for (size_t i = 0; i <= size - pattern_size; ++i) {
         if (std::memcmp(base + i, pattern, pattern_size) == 0) {
            matches.push_back(const_cast<std::byte*>(base + i));
            if (stop_at_first) break;
         }
      }
      return matches;
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<std::byte>& pattern, bool stop_at_first)
   {
      return ScanMemoryForPattern(base, size, pattern.data(), pattern.size(), stop_at_first);
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<uint8_t>& pattern, bool stop_at_first)
   {
      return ScanMemoryForPattern(base, size, reinterpret_cast<const std::byte*>(pattern.data()), pattern.size(), stop_at_first);
   }

   void* VirtualAllocNear(void* target, size_t size, DWORD protect)
   {
      // Avoid messes with ReShade redefining min/max
      auto localMin = [](auto a, auto b) { return (a < b) ? a : b; };
      auto localMax = [](auto a, auto b) { return (a > b) ? a : b; };

      SYSTEM_INFO sys_info;
      GetSystemInfo(&sys_info);

      constexpr uintptr_t two_gb = 0x80000000ull;

      uintptr_t start_addr = reinterpret_cast<uintptr_t>(target);
      uintptr_t min_addr = localMax(reinterpret_cast<uintptr_t>(sys_info.lpMinimumApplicationAddress), (start_addr > two_gb) ? (start_addr - two_gb) : 0); // The start of valid address, including this one
      uintptr_t max_addr = localMin(reinterpret_cast<uintptr_t>(sys_info.lpMaximumApplicationAddress), start_addr + two_gb); // This address is not allocatable, the last valid one is the one before
      // If memory is executable, remove the allocated size from the max searchable range, because all the memory we allocation should be within 2GB of the starting point (e.g. so we can jump pack to the original point).
      // This should be optional but whatever.
      constexpr DWORD execute_mask = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
      if ((protect & execute_mask) != 0)
         max_addr -= size - 1;

      MEMORY_BASIC_INFORMATION mbi{};
      uintptr_t addr = min_addr;

      while (addr < max_addr)
      {
         if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

         uintptr_t block_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
         uintptr_t block_end = block_start + mbi.RegionSize;

         if (mbi.State == MEM_FREE)
         {
            // Even if the whole memory region is available, simply allocate at the start of it (or anyway the first valid point that is valid)
            addr = localMax(block_start, min_addr);
            do
            {
               if (addr + size <= localMin(block_end, max_addr))
               {
                  // Try to allocate at this region's base address. This can fail for any reason.
                  // It also doesn't exactly allocate where we say so it needs to be tested.
                  void* ptr = VirtualAlloc(reinterpret_cast<LPVOID>(addr), size, MEM_RESERVE | MEM_COMMIT, protect);
                  if (ptr)
                  {
                     if (reinterpret_cast<uintptr_t>(ptr) >= min_addr && reinterpret_cast<uintptr_t>(ptr) < max_addr)
                     {
                        // Theoretically this test can't fail, but we do it anyway for extra safety
                        int64_t offset_test = reinterpret_cast<uintptr_t>(ptr) - start_addr;
                        if (offset_test >= INT32_MIN && offset_test <= INT32_MAX) // TODO: I'm not 100% these tests match with the "two_gb" limits, but I guess so
                           return ptr; // good
                     }
                     addr = reinterpret_cast<uintptr_t>(ptr);
                     VirtualFree(ptr, 0, MEM_RELEASE); // bad
                     break; // This could cause infinite loops given that VirtualAlloc() rounds down
                  }
                  addr += 1; // Slow but best chances of finding a match (I think)
               }
               else
               {
                  break;
               }
            } while (true);
         }

         // Move to the next region
         addr = block_end;
      }

      return nullptr; // No suitable block found
   }
}