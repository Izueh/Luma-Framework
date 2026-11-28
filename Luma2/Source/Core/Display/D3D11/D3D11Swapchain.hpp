#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Display/DisplayComposition.hpp"
#include "Display/DXGI/DXGISwapchain.hpp"

// The swapchain's D3D11 part: the game's back buffer, and composition on the immediate context. Inside Luma's scope.
namespace Luma {
namespace Display {
namespace Swapchain {
namespace D3D11 {
// What composition keeps between presents: the view it reads, and the texture it reads through when the source can't be read
// directly (in place, or a multisampled buffer)
struct Composition
{
   Microsoft::WRL::ComPtr<ID3D11Texture2D> copy;
   Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
   Microsoft::WRL::ComPtr<ID3D11Resource> viewed;
};

// The game's back buffer (D3D11 swapchains have one the game writes, whatever their count), with a mirror when the upgrade rules match it
Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateBuffer(ID3D11Device* device, const DXGI::BufferDesc& desc);
// `game_buffer` (Mirror) into the real buffer, or the real buffer into itself (in place: null). False when Luma's shaders weren't ready:
// a Mirror buffer then gets the game's image copied, or black.
bool Compose(IDXGISwapChain* swapchain, ID3D11Device* device, ID3D11Texture2D* game_buffer, Composition& composition, const Display::Composition::Pass& pass);
// Before Luma resizes the real buffers: nothing of Luma's refers to them
void PrepareResize(ID3D11Device* device, Composition& composition);
} // namespace D3D11
} // namespace Swapchain
} // namespace Display
} // namespace Luma
