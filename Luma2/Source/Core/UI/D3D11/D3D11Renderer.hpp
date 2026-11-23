#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include "UI/OverlayEncoding.hpp"

struct ImDrawData;

namespace Luma {
namespace UI {
namespace D3D11 {
// Never: one renderer serves every swapchain of the device
bool NeedsRecreation(const DXGI_SWAP_CHAIN_DESC& desc);
// Before ImGui's frame. Creates the renderer the first time.
bool NewFrame(ID3D11Device* device, const DXGI_SWAP_CHAIN_DESC& desc);
// Encoded for `output` (see Encoding)
void Render(ID3D11Device* device, IDXGISwapChain* swapchain, ImDrawData* data, const Encoding::Output& output);
void Shutdown();
} // namespace D3D11
} // namespace UI
} // namespace Luma
