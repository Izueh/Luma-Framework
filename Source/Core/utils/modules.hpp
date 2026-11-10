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
// `name` is an export name, or an ordinal through MAKEINTRESOURCEA.
void* FindExport(HMODULE module, const char* name);

// File name of the module containing an address, "?" if none. Takes the loader lock.
std::string GetName(const void* address);
} // namespace Modules
} // namespace Utils
} // namespace Luma
