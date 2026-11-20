#pragma once

#include <Windows.h>

namespace Luma {
namespace System {
namespace Loader {
// From DllMain, under the loader lock. Last, loads ReShade if it's next to Luma (ReShade::Load).
void Attach(HMODULE module);
// From DllMain, under the loader lock (Startup::Unload). Nothing at process exit.
void Detach(bool is_process_terminating);
} // namespace Loader
} // namespace System
} // namespace Luma
