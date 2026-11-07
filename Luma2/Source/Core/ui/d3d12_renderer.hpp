#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi.h>

struct ImDrawData;

namespace Luma {
namespace UI {
namespace D3D12 {
// It exists, for another format or buffer count
bool NeedsRecreation(DXGI_FORMAT format, uint32_t buffer_count);
// Before ImGui's frame, inside a Luma scope. Creates the renderer the first time.
bool NewFrame(ID3D12CommandQueue* queue, DXGI_FORMAT format, uint32_t buffer_count);
void Render(ID3D12CommandQueue* queue, IDXGISwapChain* swapchain, ImDrawData* data);
// Waits for its work first
void Shutdown();
} // namespace D3D12
} // namespace UI
} // namespace Luma
