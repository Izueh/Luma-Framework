#pragma once

#include <optional>

#include <dxgi1_6.h>

// The swapchain the game holds, as in ReShade: forwards to the real one, so Luma's present work runs before any other tool's Present hook
namespace Luma {
namespace Hooks {
namespace SwapchainProxy {
// `*swapchain` (the real one, just created) replaced by a proxy holding its reference
void Wrap(IDXGISwapChain** swapchain);
void Wrap(IDXGISwapChain1** swapchain);
// The real swapchain behind a proxy, or `swapchain`
IDXGISwapChain* Unwrap(IDXGISwapChain* swapchain);
// The game holds a proxy of this real swapchain
bool HasProxy(IDXGISwapChain* swapchain);
// This thread is in a call a proxy forwards: the game's
bool IsForwarding();
// Luma's own calls, through the proxy so tools hooking it see them (not as the game's)
HRESULT ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
HRESULT SetColorSpace1(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
HRESULT SetHDRMetaData(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata);
} // namespace SwapchainProxy

namespace DXGI {
struct Prologue
{
   bool is_game = false; // The game's present (not a test, not inside Luma)
   bool ran = false; // Luma's work ran (it doesn't when the last present of the swapchain was still drawing)
};

// Luma's work for the game's present, `flags` can gain tearing
Prologue PresentPrologue(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags);
void PresentEpilogue(IDXGISwapChain* swapchain, const Prologue& prologue, HRESULT result);
// The proxy's Present: the prologue unless it ran, then the real Present
HRESULT PresentForGame(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters, bool present1,
   std::optional<Prologue> prologue = std::nullopt);
} // namespace DXGI
} // namespace Hooks
} // namespace Luma
