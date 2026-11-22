#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <Windows.h>

// Wraps ReShade add-on event handlers in a Luma scope and tracks add-on module ranges so their D3D calls bypass Luma's hooks.
namespace Luma {
namespace ReShade {
namespace AddonEvents {
// Installs hooks on ReShade's registration exports when ReShade is loaded.
void Install(HMODULE reshade, bool registered_before_luma);

void OnModuleLoaded(std::wstring_view name, const void* base, size_t size);
void OnModuleUnloaded(const void* base);

// True if `address` is inside a tracked add-on module (never Luma or ReShade itself).
bool IsAddonAddress(const void* address);

std::string Describe();
} // namespace AddonEvents
} // namespace ReShade
} // namespace Luma
