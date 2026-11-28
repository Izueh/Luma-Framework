#pragma once

namespace Luma {
namespace Startup {
// Once, under the loader lock, when a graphics dll loads: the game's declarations, the settings, then the hooks they need. Pins Luma's dll
void Initialize();
bool IsInitialized();
// DllMain's DLL_PROCESS_DETACH for a FreeLibrary of a Luma that never initialized (never process exit): unhooks without waiting for any thread
void Unload();
} // namespace Startup
} // namespace Luma
