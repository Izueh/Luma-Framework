#pragma once

#include <Windows.h>

namespace Luma {
namespace System {
namespace Loader {
// From DllMain, under the loader lock
void Attach(HMODULE module);
// From DllMain, under the loader lock (Startup::Unload). Nothing at process exit.
void Detach(bool is_process_terminating);
} // namespace Loader
} // namespace System
} // namespace Luma
