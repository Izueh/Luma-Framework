#pragma once

#include <Windows.h>

namespace Luma {
namespace System {
namespace Loader {
// Registers a loader notification. Core initializes when d3d11.dll or d3d12.dll loads, still under the loader lock.
void Attach(HMODULE module);
void Detach(bool is_process_terminating);
} // namespace Loader
} // namespace System
} // namespace Luma
