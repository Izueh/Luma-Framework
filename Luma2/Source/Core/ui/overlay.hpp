#pragma once

#include <filesystem>

#include <dxgi.h>
#include <Windows.h>

#include "common/api.hpp"

namespace Luma {
namespace UI {
// Saves the settings to the path when one changes. Once at startup, after they're loaded.
void SaveSettingsTo(const std::filesystem::path& path);

// Attaches, if the overlay isn't attached to another swapchain. `device` is the real device.
void OnSwapchainCreated(IDXGISwapChain* swapchain, const Presenter& presenter, const void* device);
// Before the game's present
void OnPresent(IDXGISwapChain* swapchain);
void OnSwapchainDestroyed(const void* swapchain);
void OnDeviceReleased(const void* device);
// From the window's thread
void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

bool IsOpen();
void SetOpen(bool open);
const void* GetSwapchain();
} // namespace UI
} // namespace Luma
