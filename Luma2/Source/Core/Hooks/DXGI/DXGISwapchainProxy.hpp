#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <dxgi1_6.h>

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

// What ReShade's calls on the game's swapchain get. Its wrapper forwards the game's calls and its own reads from the same module, so a
// call from it is ReShade's own read only inside a window on the thread that creates, resizes or presents the swapchain:
// * the init window: from the proxy's creation, ResizeBuffers(1) or SetColorSpace1 returning to ReShade, to its init_effect_runtime event
//   (its runtime reads the buffers, their count and the color space in between). It also closes at any other method of the proxy, at
//   Present and at the next creation or resize, and a read in it counts only when the code calling ReShade's is ReShade's too;
// * the present window: from its present event to Luma's Present (D3D12 reads the buffer index each frame).
enum class ReShadeSwapchainView : uint8_t
{
   SameAsOverlay, // Not Mirror: ReShade's calls are an overlay's (the buffers it gets are the real ones anyway)
   SameAsGame, // Mirror, the bridge off: all of them get the game's view, so its effects draw before the composition
   RealInsideWindows, // Mirror, the bridge on: reads inside a window get the real buffers, index, description and color space
};

// ReShade's module, once it's known (ReShade::Load), and whether its bridge is on
void SetReShadeModule(const void* base, size_t size);
void SetReShadeBridged(bool bridged);
bool IsReShadeBridged();
// At ReShade's init_effect_runtime event: this thread's init window ends
void CloseReShadeInitWindow();

struct ResizeArguments
{
   UINT count = 0;
   UINT width = 0;
   UINT height = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   UINT flags = 0;
};

// Luma's own resize (an output switch) through ReShade's wrapper, found in the real swapchain's private data: its runtime holds views on
// the real buffers, which only its own ResizeBuffers lets go of. ReShade sees `game` (what the game's own resize would pass), the real
// swapchain gets `real`, and once it's resized ReShade's runtime re-reads it, its color space answered as `color_space`. Empty when there
// is no wrapper (logged once per swapchain).
std::optional<HRESULT> ResizeBuffersThroughReShade(IDXGISwapChain* swapchain, const ResizeArguments& game, const ResizeArguments& real,
   DXGI_COLOR_SPACE_TYPE color_space);
// The same for a color space change without a resize: ReShade's runtime reads the color space only when its own SetColorSpace1 or
// ResizeBuffers resets it. Also empty when ReShade didn't pass the call on.
std::optional<HRESULT> SetColorSpace1ThroughReShade(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
} // namespace SwapchainProxy
} // namespace Hooks
} // namespace Luma
