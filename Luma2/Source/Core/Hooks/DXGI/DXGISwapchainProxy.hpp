#pragma once

#include <optional>

#include <dxgi1_6.h>

// The swapchain the game holds when Luma owns the real one (ReShade's model): an IDXGISwapChain4 forwarding to the real swapchain.
// It exists for order and identity, its logic stays in the hooks and handlers:
// * Present: Luma's work (composition, its overlay) runs in the proxy's Present, before the real Present, so every hook another tool
//   put in DXGI's Present runs after it and draws on the final image, whatever the install order. A tool that hooks the proxy itself
//   (Steam hooks the swapchain the game holds) is kept after Luma too: each proxy has its own vtable, whose Present slots hold Luma's
//   outer Present, which composes and then calls what's next (a tool's patched slot, the proxy's own Present, a tool's patch of it);
//   a tool replacing the slot is put back after Luma at the next present;
// * the game's view: a call the proxy forwards is the game's (IsForwarding), answered with what the game asked for (its back buffers,
//   its desc, its color space kept); anyone calling the real swapchain (an overlay, a capture tool) gets the real one.
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
// Luma's own resize of the real swapchain (an output mode change): through the proxy where there is one, so a tool that hooked the
// swapchain the game holds (Steam does) sees it and lets go of its buffers first (it fails otherwise), but not as the game's call
HRESULT ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
// The same for the color space and HDR metadata Luma sets: a tool that hooked the proxy (Steam) encodes its drawing for what it saw set
HRESULT SetColorSpace1(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
HRESULT SetHDRMetaData(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata);
} // namespace SwapchainProxy

namespace DXGI {
// Luma's handling of the game's present (composition, the overlay): false when it isn't the game's (a test). `flags` can gain tearing.
bool PresentPrologue(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags);
// The proxy's Present: the prologue unless `prologue` says it ran (and whether it was the game's), then the real one through its vtable
// (every tool's hook) inside Luma's scope. In DXGIHooks.cpp.
HRESULT PresentForGame(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters, bool present1,
   std::optional<bool> prologue = std::nullopt);
// An overlay asked the proxy for a buffer or the index (Swapchain::DXGI::ComposeForOverlay). In DXGIHooks.cpp.
void OnOverlayBufferRequest(IDXGISwapChain* swapchain);
} // namespace DXGI
} // namespace Hooks
} // namespace Luma
