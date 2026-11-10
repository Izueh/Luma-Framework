#pragma once

#include <Windows.h>

namespace Luma {
namespace Loader {
// Runs `initialize` once d3d11.dll is loaded (now, if it already is), under the loader lock
void Attach(HMODULE module, void (*initialize)());
// Not on process exit
void Detach();
} // namespace Loader
} // namespace Luma
