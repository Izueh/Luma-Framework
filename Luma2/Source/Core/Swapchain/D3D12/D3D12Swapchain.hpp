#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Display/D3D12/D3D12Composition.hpp"
#include "Swapchain/DXGI/DXGISwapchain.hpp"

// The swapchain's D3D12 part: the game's back buffers, and composition on the presenting queue. Inside Luma's scope.
namespace Luma {
namespace Swapchain {
namespace D3D12 {
// One of the game's back buffers, in the common state (a back buffer's at creation), with a mirror when the upgrade rules match it
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, const DXGI::BufferDesc& desc, UINT index);
// `game_buffer` (Mirror) into the real buffer presented next, or that buffer into itself (in place: null)
bool Compose(IDXGISwapChain* swapchain, Display::Composition::D3D12::Renderer& renderer, ID3D12Resource* game_buffer, const Display::Composition::Pass& pass);
} // namespace D3D12
} // namespace Swapchain
} // namespace Luma
