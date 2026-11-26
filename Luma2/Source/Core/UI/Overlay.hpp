#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>

#include <Windows.h>

namespace Luma {
namespace UI {
// Registers the overlay's settings. Once at startup, before the settings are loaded.
void Initialize();
// Saves the settings to the path when one changes. Once at startup, after they're loaded.
void SaveSettingsTo(const std::filesystem::path& path);

// What the overlay is drawn into, in pixels
struct Extent
{
   uint32_t width = 0;
   uint32_t height = 0;
};

// For the presentation that draws the overlay (UI::DXGI): the lock every ImGui call takes, and one ImGui context for the window
[[nodiscard]] std::unique_lock<std::recursive_mutex> Lock();
// With the lock held. False if another window has the context.
bool AttachWindow(HWND hwnd);
void DetachWindow();
// Presenting thread, before a present's overlay: false when there's nothing to draw (closed, no toast)
bool BeginPresent();
// With the lock held, between the renderer's NewFrame and Render
void BuildFrame(Extent extent);
// With the lock held, after a drawn frame: the layout and the settings saved once an edit is done
void EndFrame();

// From the window's thread
void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
// The mouse's clicks and wheel from raw input ("usButtonFlags" and "usButtonData"), which the backend ignores
void OnRawMouse(USHORT button_flags, USHORT button_data);

bool IsOpen();
void SetOpen(bool open);
// Clears the saved ImGui layout from Luma.toml and restores default window position/size
void ResetLayout();
} // namespace UI
} // namespace Luma
