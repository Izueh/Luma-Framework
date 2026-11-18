#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <Windows.h>

namespace Luma {
namespace Utils {
namespace Modules {
void SetSelf(HMODULE module);
// Luma's own dll
HMODULE Self();
std::filesystem::path GetPath(HMODULE module);
std::filesystem::path SelfDirectory();

// Full path of a System32 dll
std::filesystem::path SystemPath(std::wstring_view name);
// If it's loaded. Never a dll of the same name elsewhere, like Luma's own proxy.
HMODULE FindSystemModule(std::wstring_view name);

// An export's address from the module's export table, forwarders followed. Not GetProcAddress: tools that hook it
// (RenderDoc) would hand back their hook, which forwards to the first "dxgi.dll", Luma's proxy, in a loop.
// `name` is an export name or an ordinal (MAKEINTRESOURCEA). Never loads a dll (runs under the loader lock): a forwarder to one not
// loaded yet gives null, with `waits_for_dll` set.
void* FindExport(HMODULE module, const char* name, bool* waits_for_dll = nullptr);

// File name of the module containing an address, "?" if none. Takes the loader lock.
std::string GetName(const void* address);
// A dll of the system directory (System32, or SysWOW64 for a 32-bit process). Takes the loader lock.
bool IsSystemModule(HMODULE module);
// Whether the address is in a dll of the system directory (an import stub's destination, never a hooker's). Takes the loader lock.
bool IsInSystemModule(const void* address);
bool IsLoaderLockHeld();

void Pin();
bool IsPinned();
} // namespace Modules
} // namespace Utils
} // namespace Luma
