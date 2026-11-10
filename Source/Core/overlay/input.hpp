#pragma once

#include <cstdint>
#include <string>

#include <Windows.h>

namespace Luma {
namespace Overlay {
namespace Input {
void Attach(HWND window);
// Gives the window its procedure back, unless another subclass went on top: then it stays, passing messages through
void Detach(HWND window);

// A message for the attached window (or one of its children), from its procedure or from the message queue hooks.
// Handles the toggle key and feeds the overlay while it's open. True if the game must not see it.
bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

uint32_t GetToggleKey();
// The next key pressed becomes the toggle key (Escape cancels)
void CaptureToggleKey();
bool IsCapturingToggleKey();
// On the render thread: the config isn't written from the window's thread
void ApplyCapturedKey();
std::string GetKeyName(uint32_t virtual_key);
} // namespace Input
} // namespace Overlay
} // namespace Luma
