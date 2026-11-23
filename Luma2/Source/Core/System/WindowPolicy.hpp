#pragma once

#include <Windows.h>

namespace Luma {
namespace System {
namespace Window {
// The window loses its borders and is centered on its monitor, shrunk to it if it's bigger. In
// place of exclusive fullscreen. Doesn't wait for the window's thread.
void MakeBorderless(HWND window);
// Restores the style and rect saved before MakeBorderless changed `window`
void RestoreBorderless(HWND window);
// Hides WM_KILLFOCUS, WM_ACTIVATE (WA_INACTIVE), and WM_ACTIVATEAPP (FALSE) from `window` (and any previously tracked
// swapchain window) while enabled, so Alt-Tabbing doesn't pause the game. Live: can be toggled at runtime without resetting.
void SetFocusLossSuppression(HWND window, bool enabled);
void SetFocusLossSuppression(bool enabled);
} // namespace Window
} // namespace System
} // namespace Luma
