#pragma once

#include <Windows.h>

namespace Luma {
namespace Overlay {
namespace InputWindows {
// Declares the user32.dll hooks
void Install();
// Releases the game's cursor clip while the overlay is open, and restores it afterwards
void OnOpenChanged(bool open);

// What the game registered for raw input, as seen by "RegisterRawInputDevices"
constexpr unsigned int RAW_INPUT_NO_LEGACY_KEYBOARD = 0x1; // "RIDEV_NOLEGACY" for the keyboard: the window gets no "WM_KEYDOWN" and friends
constexpr unsigned int RAW_INPUT_NO_LEGACY_MOUSE = 0x2;    // The same for the mouse: no button or wheel messages

// If `window` is the target of a raw input registration (its "WM_INPUT" messages are input to block)
bool IsRawInputWindow(HWND window);
// Registrations without a target window (they follow the focus) count for every window
unsigned int GetRawInputFlags(HWND window);
} // namespace InputWindows
} // namespace Overlay
} // namespace Luma
