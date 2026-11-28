#pragma once

#include <optional>

#include <dxgi1_6.h>

#include "Hooks/DXGI/DXGISwapchainProxy.hpp"

namespace Luma {
namespace Hooks {
namespace DXGI {
// Luma's handling of the game's present (composition, the overlay): false when it isn't the game's (a test). `flags` can gain tearing.
// The present work (up to the composition) is skipped when ReShade's present event ran it for this swapchain.
bool PresentPrologue(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags);
// At ReShade's present event, before its effects: Luma's present work for the Present that follows on this thread, then the window for
// ReShade's work opens. False when it didn't run: inside Luma's scope, or a swapchain whose Present Luma doesn't see.
bool RunPresentWorkAtEvent(IDXGISwapChain* swapchain);
// First in every Luma Present: closes that window, so the prologue that follows skips the present work the event ran
void CloseThirdPartyWorkWindowAtPresent();
// At ReShade's finish_present event: true when no Luma Present came to close the window, which is closed here
bool CloseThirdPartyWorkWindowAtFinishPresent();
// The proxy's Present: the prologue unless `prologue` says it ran (and whether it was the game's), then the real one through its vtable
// (every tool's hook) inside Luma's scope
HRESULT PresentForGame(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters, bool present1,
   std::optional<bool> prologue = std::nullopt);
// An overlay asked the proxy for a buffer or the index (Display::Swapchain::DXGI::ComposeForOverlay)
void OnOverlayBufferRequest(IDXGISwapChain* swapchain);
// What ReShade's calls on this swapchain get (Display::Swapchain::DXGI)
SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain);
// ReShade's present event ran the present work of this swapchain on this thread, and Luma's Present hasn't come yet
bool IsThirdPartyWorkWindowOpenFor(IDXGISwapChain* swapchain);
} // namespace DXGI
} // namespace Hooks
} // namespace Luma
