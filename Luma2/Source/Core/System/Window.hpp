#pragma once

#include <Windows.h>

namespace Luma {
namespace System {
namespace Window {
// 1.0's CenterWindowAndRemoveBorders: the window loses its borders and is centered on its monitor, shrunk to it if it's bigger. In
// place of exclusive fullscreen. Doesn't wait for the window's thread.
void MakeBorderless(HWND window);
} // namespace Window
} // namespace System
} // namespace Luma
