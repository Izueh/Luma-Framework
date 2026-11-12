#pragma once

namespace Luma {
namespace Core {
// Once, under the loader lock: the game's declarations, the settings, then the hooks they need
void Initialize();
bool IsInitialized();
// When Luma is unloaded, never at process exit
void Shutdown();
} // namespace Core
} // namespace Luma
