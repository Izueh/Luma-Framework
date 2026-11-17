#pragma once

#include <filesystem>

#include <dxgi.h>
#include <Windows.h>

#include "Common/Api.hpp"

namespace Luma {
namespace UI {
// Registers the overlay's settings. Once at startup, before the settings are loaded.
void Initialize();
// Saves the settings to the path when one changes. Once at startup, after they're loaded.
void SaveSettingsTo(const std::filesystem::path& path);

// Attaches, if the overlay isn't attached to another swapchain. `device` is the real device.
void AttachOverlay(IDXGISwapChain* swapchain, const Presenter& presenter, const void* device);
// Before the game's present
void DrawOverlay(IDXGISwapChain* swapchain);
void DetachFromSwapchain(const void* swapchain);
void DetachFromDevice(const void* device);
// From the window's thread
void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
// The mouse's clicks and wheel from raw input ("usButtonFlags" and "usButtonData"), which the backend ignores
void OnRawMouse(USHORT button_flags, USHORT button_data);

bool IsOpen();
void SetOpen(bool open);
const void* GetSwapchain();
} // namespace UI
} // namespace Luma
