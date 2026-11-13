#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi.h>

#include "UI/Encoding.hpp"

struct ImDrawData;

namespace Luma {
namespace UI {
namespace D3D12 {
// It exists, for another format or buffer count
bool NeedsRecreation(const DXGI_SWAP_CHAIN_DESC& desc);
// Before ImGui's frame, inside a Luma scope. Creates the renderer the first time.
bool NewFrame(ID3D12CommandQueue* queue, const DXGI_SWAP_CHAIN_DESC& desc);
// Encoded for `output` (see Encoding)
void Render(ID3D12CommandQueue* queue, IDXGISwapChain* swapchain, ImDrawData* data, const Encoding::Output& output);
// Waits for its work first
void Shutdown();
} // namespace D3D12
} // namespace UI
} // namespace Luma
