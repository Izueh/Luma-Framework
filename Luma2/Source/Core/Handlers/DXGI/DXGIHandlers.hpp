#pragma once

#include <optional>

#include <dxgi1_6.h>

#include "Common/Api.hpp"
#include "Hooks/DXGI/DXGISwapchainProxy.hpp"

// See D3D11Handlers.hpp. `creator` is the real D3D11 device or a D3D12 command queue.
namespace Luma {
namespace Handlers {
void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc, std::optional<Api> known_api = std::nullopt);
void PreCreateSwapChainForHwnd(IUnknown* creator, DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd);
// An overlay asked the game's proxy for a buffer or the index: it gets the real ones, composed first on the presenting thread
void OnOverlayBufferRequest(IDXGISwapChain* swapchain);
// After PostCreateSwapChain: the game gets a proxy of it (Hooks::SwapchainProxy)
bool ShouldWrapSwapChain(IDXGISwapChain* swapchain);
// What ReShade's calls on the game's proxy of it get (Swapchain::DXGI)
Hooks::SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain);
// Reporting a swapchain twice is harmless
void PostCreateSwapChain(IDXGISwapChain* swapchain, IUnknown* creator);
void OnSwapChainDestroyed(const IDXGISwapChain* swapchain);
// `node_masks` and `present_queues` are only set by ResizeBuffers1
void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT& width, UINT& height, DXGI_FORMAT& format, UINT& flags, const UINT* node_masks,
   IUnknown* const* present_queues);
// Also after a resize that failed
void PostResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, bool succeeded);
// True skips the call
bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output);
// The game's calls Luma answers itself (Swapchain::DXGI): empty or false lets them through
// `third_party`: the caller is an overlay's code
std::optional<HRESULT> GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer, bool third_party);
std::optional<UINT> GetCurrentBackBufferIndex(IDXGISwapChain* swapchain, bool third_party);
std::optional<BOOL> GetFullscreenState(IDXGISwapChain* swapchain);
bool PreSetColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
bool PreSetHdrMetadata(IDXGISwapChain* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, const void* metadata);
// What the game is told about the display (Display::Spoofing)
void PostCheckColorSpaceSupport(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space, UINT& support);
void PostGetOutputDesc1(IDXGIOutput6* output, DXGI_OUTPUT_DESC1& desc);
// A color space was set on the real swapchain (by the game, or by Luma where it owns it)
void PostSetColorSpace(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
// What the game last set, empty if it never did (DXGI then uses its default for the format)
std::optional<DXGI_COLOR_SPACE_TYPE> GetColorSpace(IDXGISwapChain* swapchain);
// Luma's present work, up to and including the display composition: at ReShade's present event, before its effects, when its bridge is
// on, otherwise in PrePresent
void PrePresentWork(IDXGISwapChain* swapchain);
// Not for DXGI_PRESENT_TEST. `flags` can gain tearing. `present_work_done`: ReShade's present event ran PrePresentWork for this present;
// Luma's overlay is drawn here either way, after ReShade's effects and overlay.
void PrePresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags, bool present_work_done);
// Composition wrote the whole buffer: Present1's dirty rects and scroll don't apply
bool ComposesWholeBuffer(IDXGISwapChain* swapchain);
// After a present that succeeded
void PostPresent(IDXGISwapChain* swapchain);
// The game released this device: its swapchains are no longer reported to the game, even while they're alive
void ForgetSwapchains(const void* device);
} // namespace Handlers
} // namespace Luma
