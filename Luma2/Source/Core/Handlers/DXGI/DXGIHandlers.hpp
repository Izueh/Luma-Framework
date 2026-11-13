#pragma once

#include <optional>

#include <dxgi1_4.h>

// See D3D11Handlers.hpp. `creator` is the real D3D11 device or a D3D12 command queue.
namespace Luma {
namespace Handlers {
void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc);
void PreCreateSwapChainForHwnd(IUnknown* creator, DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd);
// Reporting a swapchain twice is harmless
void PostCreateSwapChain(IDXGISwapChain* swapchain, IUnknown* creator);
void OnSwapChainDestroyed(const IDXGISwapChain* swapchain);
// `node_masks` and `present_queues` are only set by ResizeBuffers1
void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT& flags, const UINT* node_masks,
   IUnknown* const* present_queues);
void PostResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
// True skips the call
bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output);
// The game set the swapchain's color space
void PostSetColorSpace(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
// What the game last set, empty if it never did (DXGI then uses its default for the format)
std::optional<DXGI_COLOR_SPACE_TYPE> GetColorSpace(IDXGISwapChain* swapchain);
// Not for DXGI_PRESENT_TEST
void PrePresent(IDXGISwapChain* swapchain);
void PostPresent(IDXGISwapChain* swapchain);
// The game released this device: its swapchains are no longer reported to the game, even while they're alive
void ForgetSwapchains(const void* device);
} // namespace Handlers
} // namespace Luma
