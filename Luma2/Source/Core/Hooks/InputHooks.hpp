#pragma once

#include <cstdint>

#include <Windows.h>

namespace Luma {
namespace Hooks {
namespace Input {
// What the game registered for raw input ("RIDEV_NOLEGACY"): the window gets no "WM_KEYDOWN", "WM_LBUTTONDOWN" and friends
constexpr uint32_t RAW_INPUT_NO_LEGACY_KEYBOARD = 0x1;
constexpr uint32_t RAW_INPUT_NO_LEGACY_MOUSE = 0x2;

// Declares the hooks that keep the game from seeing input while the overlay is open: the message loop, the cursor, key
// states, raw input, Windows hooks and DirectInput (1 to 8). DirectInput's are installed once the game loads its dll.
void Install();

// Tells the hooks the overlay opened or closed. Also releases the game's cursor clip while it's open, and restores it afterwards.
void OnOpenChanged(bool open);
// The overlay is open, for any caller: inside a Luma scope too, where IsBlocking is false
bool IsOverlayOpen();

// The overlay is open and the call is the game's: Luma's own calls (inside a scope, e.g. ImGui's backend) are never blocked
bool IsBlocking();

// Who gets the messages the game takes off its queue (the overlay). Returns true if the game must not see the message.
using MessageHandler = bool (*)(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
void SetMessageHandler(MessageHandler handler);
bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

// If the game registered `window` as the target of raw input (its "WM_INPUT" messages go to it)
bool IsRawInputWindow(HWND window);
// The "RAW_INPUT_*" flags of what the game registered for `window`, and for no window in particular (it follows the focus)
uint32_t GetRawInputFlags(HWND window);
} // namespace Input
} // namespace Hooks
} // namespace Luma
