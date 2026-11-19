#pragma once

#include <dxgi1_6.h>

// The swapchain the game holds when Luma owns the real one (ReShade's model): an IDXGISwapChain4 forwarding to the real swapchain.
// It exists for order and identity, its logic stays in the hooks and handlers:
// * Present: Luma's work (composition, its overlay) runs in the proxy's Present, before the real Present, so every hook another tool
//   put in DXGI's Present runs after it and draws on the final image, whatever the install order;
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
} // namespace SwapchainProxy

namespace DXGI {
// The proxy's Present: Luma's handling, then the real one through its vtable (every tool's hook) inside Luma's scope. In DXGIHooks.cpp.
HRESULT PresentForGame(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters, bool present1);
// An overlay asked the proxy for a buffer or the index (Swapchain::DXGI::ComposeForOverlay). In DXGIHooks.cpp.
void OnOverlayBufferRequest(IDXGISwapChain* swapchain);
} // namespace DXGI
} // namespace Hooks
} // namespace Luma
