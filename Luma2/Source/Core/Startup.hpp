#pragma once

#include "Hooks/Functions.hpp"

namespace Luma {
namespace Startup {
// Once, under the loader lock, when a graphics dll loads: the game's declarations, the settings, then the hooks they need. Pins Luma's dll
void Initialize();
bool IsInitialized();
// DllMain's DLL_PROCESS_DETACH for a FreeLibrary of a Luma that never initialized (never process exit): unhooks without waiting for any thread
void Unload();

// Every function a Core feature may hook, needed this run or not. The game gets events for these, never native callbacks.
Hooks::D3D11::Functions GetOwnedFunctions();
} // namespace Startup
} // namespace Luma
