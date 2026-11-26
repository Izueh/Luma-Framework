#pragma once

#include "Swapchain/DXGI/DXGIPresenter.hpp"

struct IDXGISwapChain;

namespace Luma {
namespace UI {
namespace DXGI {
// At each present, before DrawOverlay: the overlay goes to the first eligible swapchain to present (a window's, not a probe's), and to
// another only once that one stopped presenting. `device` is the real device.
void AttachOverlay(IDXGISwapChain* swapchain, const Swapchain::DXGI::Presenter& presenter, const void* device);
// Before the game's present
void DrawOverlay(IDXGISwapChain* swapchain);
void DetachFromSwapchain(const void* swapchain);
void DetachFromDevice(const void* device);
const void* GetSwapchain();
} // namespace DXGI
} // namespace UI
} // namespace Luma
