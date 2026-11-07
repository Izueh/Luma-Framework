#pragma once

#include <d3d11.h>
#include <dxgi.h>

struct ImDrawData;

namespace Luma {
namespace UI {
namespace D3D11 {
// Before ImGui's frame. Creates the renderer the first time.
bool NewFrame(ID3D11Device* device);
void Render(ID3D11Device* device, IDXGISwapChain* swapchain, ImDrawData* data);
void Shutdown();
} // namespace D3D11
} // namespace UI
} // namespace Luma
