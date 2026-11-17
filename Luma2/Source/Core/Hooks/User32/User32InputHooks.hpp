#pragma once

#include <cstdint>

#include <Windows.h>

namespace Luma {
namespace Hooks {
namespace Input {
namespace User32 {
// Declares the user32.dll hooks
void Install();
// Releases the game's cursor clip while the overlay is open, and restores it afterwards
void OnOpenChanged(bool open);

// If the game registered `window` as the target of raw input
bool IsRawInputWindow(HWND window);
// The "RAW_INPUT_*" flags of what the game registered for `window`, and for no window in particular (it follows the focus)
uint32_t GetRawInputFlags(HWND window);
} // namespace User32
} // namespace Input
} // namespace Hooks
} // namespace Luma
