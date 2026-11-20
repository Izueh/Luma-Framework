#pragma once

#include <Windows.h>

namespace Luma {
namespace Loader {
// Runs `initialize` once d3d11.dll is loaded (now, if it already is), under the loader lock. Then loads ReShade if it's next to Luma.
void Attach(HMODULE module, void (*initialize)());
// Not on process exit. Bypasses and unhooks without waiting (Hooks::UninstallAll).
void Detach();
} // namespace Loader
} // namespace Luma
