#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Luma {
namespace Hooks {
namespace ThirdPartyModules {
// From the loader notification, under the loader lock: logs a known tool the first time it loads
void OnModuleLoaded(std::wstring_view name, const void* base, size_t size);
void OnModuleUnloaded(const void* base);
// The dlls loaded before the notification was registered: reads their headers only
void ScanLoadedModules();
// A dll recognized some other way
void Add(std::string name, const void* base, size_t size);

// The overlay whose code `address` is in, empty if none. A relaxed load while no overlay is loaded.
std::string Find(const void* address);
// Its dll's base, 0 if none: no allocation
uintptr_t FindBase(const void* address);
std::vector<std::string> GetLoaded();
} // namespace ThirdPartyModules
} // namespace Hooks
} // namespace Luma
