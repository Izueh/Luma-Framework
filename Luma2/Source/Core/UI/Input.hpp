#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Windows.h>

namespace Luma {
namespace UI {
namespace Input {
// Registers the overlay key and the on/off key (Settings::Core::OVERLAY_KEY, ENABLED_KEY). Once at startup, before the settings are loaded.
void Initialize();

// Luma never subclasses the window, as ReShade: other layers (dgVoodoo, Steam's overlay) put their own procedures on it, and a chain that
// has Luma twice recurses until the stack overflows. Messages come from the message queue hooks.
void Attach(HWND window);
// A message for the attached window (or one of its children), from the message queue hooks.
// Handles the toggle key and feeds the overlay while it's open. True if the game must not see it.
bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
void Detach(HWND window);

// The next key pressed is taken from the game and the overlay (Escape cancels)
void CaptureKey();
bool IsCapturingKey();
// The key captured since, once. 0 while capturing or if cancelled.
uint32_t TakeCapturedKey();
// The same, the key becoming the toggle key at the next present
void CaptureToggleKey();
// On the render thread: settings aren't thread safe
void ApplyCapturedKey();
std::string GetKeyName(uint32_t virtual_key);
} // namespace Input
} // namespace UI
} // namespace Luma
