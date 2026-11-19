#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include "overlay/encoding.hpp"

struct ImDrawData;

namespace Luma {
namespace Overlay {
namespace D3D11 {
// Before ImGui's frame. Creates the renderer the first time.
bool NewFrame(ID3D11Device* device);
void Render(ID3D11Device* device, IDXGISwapChain* swapchain, ImDrawData* data, const Encoding::Output& output);
void Shutdown();
} // namespace D3D11
} // namespace Overlay
} // namespace Luma
