#pragma once

#include "Hooks/Functions.hpp"

namespace Luma {
namespace Startup {
// Once, under the loader lock: the game's declarations, the settings, then the hooks they need
void Initialize();
bool IsInitialized();
// When Luma is unloaded, never at process exit
void Shutdown();

// Every function a Core feature may hook, needed this run or not. The game gets events for these, never native callbacks.
Hooks::D3D11::Functions GetOwnedFunctions();
} // namespace Startup
} // namespace Luma
