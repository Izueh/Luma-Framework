#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Windows.h>

namespace Luma {
namespace UI {
namespace Input {
inline constexpr std::string_view TOGGLE_KEY_SETTING = "Luma.OverlayKey";

// Registers the toggle key setting. Once at startup, before the settings are loaded.
void Initialize();

void Attach(HWND window);
// Gives the window its procedure back, unless another subclass went on top: then it stays, passing messages through
void Detach(HWND window);

// The next key pressed becomes the toggle key (Escape cancels)
void CaptureToggleKey();
bool IsCapturingToggleKey();
// On the render thread: settings aren't thread safe
void ApplyCapturedKey();
std::string GetKeyName(uint32_t virtual_key);
} // namespace Input
} // namespace UI
} // namespace Luma
