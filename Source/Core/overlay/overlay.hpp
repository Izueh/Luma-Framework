#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>

namespace Luma {
namespace Overlay {
using DrawFunction = void (*)(ID3D11Device* device);

// `main` draws inside Luma's window while the overlay is open, `always` draws every frame (on screen messages)
void SetDrawFunctions(DrawFunction main, DrawFunction always);

// Attaches, if the overlay isn't attached to another swapchain. `device` is the real device.
void OnSwapchainCreated(IDXGISwapChain* swapchain, ID3D11Device* device);
// Before the game's present
void OnPresent(IDXGISwapChain* swapchain);
void OnSwapchainDestroyed(const IDXGISwapChain* swapchain);
void OnDeviceDestroyed(const void* device);
// From the window's thread
void OnWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

bool IsOpen();
void SetOpen(bool open);
} // namespace Overlay
} // namespace Luma
