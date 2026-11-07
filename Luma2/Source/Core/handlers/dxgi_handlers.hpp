#pragma once

#include <dxgi.h>

namespace Luma {
namespace Handlers {
void OnFactoryCreated(IDXGIFactory* factory);

// `creator` is the real D3D11 device or a D3D12 command queue. Reporting a swapchain twice is harmless.
void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator);
// Before the game's present (not for DXGI_PRESENT_TEST)
void OnPresent(IDXGISwapChain* swapchain);
void OnSwapchainResized(IDXGISwapChain* swapchain);
void OnFullscreenStateChanged(IDXGISwapChain* swapchain, BOOL fullscreen);
// The game released this device: its swapchains are no longer reported to the game, even while they're alive
void ForgetSwapchains(const void* device);
} // namespace Handlers
} // namespace Luma
