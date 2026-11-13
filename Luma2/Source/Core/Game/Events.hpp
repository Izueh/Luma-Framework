#pragma once

#include <cstdint>
#include <span>

#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include "Common/Api.hpp"

namespace Luma {
namespace Game {
struct SwapchainDesc
{
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   UINT buffer_count = 0;
   UINT flags = 0;
   // Kept by a resize, which ignores changes to them
   DXGI_SWAP_EFFECT swap_effect = DXGI_SWAP_EFFECT_DISCARD;
   UINT sample_count = 1;
   bool windowed = true;
   // ResizeBuffers: the format and count are the swapchain's current ones where the game kept them (zero or unknown)
   bool resize = false;
};

namespace D3D11 {
// Each runs where 1.0's Core called the game and only reports, unless its comment says so. Calls made from one aren't tracked by
// Core. Setting one installs its hook, so they are set in Init, in field order. They get the real API objects, not Luma's proxies.
struct Events
{
   // After Core's objects on the device and the GameDeviceData are created
   void (*on_init_device)(ID3D11Device* device) = nullptr;
   // The game released its last reference. First, while Core's objects are still valid; the GameDeviceData is destroyed after.
   void (*on_destroy_device)(ID3D11Device* device) = nullptr;

   // At each creation and resize, before Core's own changes (1.0's order)
   void (*on_create_swapchain)(SwapchainDesc& desc) = nullptr;
   // At the end of a creation and of each resize, the device's output resolution already set
   void (*on_init_swapchain)(IDXGISwapChain* swapchain, ID3D11Device* device, bool resize) = nullptr;
   // After Core's frame work, before the overlay is drawn and LumaSettings published. Reset per frame state here.
   void (*on_present)(IDXGISwapChain* swapchain, ID3D11Device* device) = nullptr;

   // After Core tracked it. `hash` is the game's bytecode's: shaders are changed through variants, never here.
   void (*on_create_shader)(ID3D11Device* device, ShaderStage stage, uint32_t hash, std::span<const uint8_t> bytecode, ID3D11DeviceChild* shader) = nullptr;

   // After Core records them, except constant buffers, reported first (1.0)
   void (*on_bind_constant_buffers)(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer* const* buffers) = nullptr;
   void (*on_bind_viewports)(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports) = nullptr;
   void (*on_bind_scissor_rects)(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects) = nullptr;
   void (*on_bind_blend_state)(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask) = nullptr;
   void (*on_bind_depth_stencil_state)(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref) = nullptr;
   // After Core forgot what it recorded
   void (*on_clear_state)(ID3D11DeviceContext* context) = nullptr;

   // After Core's handling
   void (*on_finish_command_list)(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list) = nullptr;
   // Before Core's handling (1.0), so the game can run its own lists first. True skips the execution.
   bool (*on_execute_command_list)(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore) = nullptr;
};
} // namespace D3D11

namespace D3D12 {
struct Events
{
   void (*on_init_device)(ID3D12Device* device) = nullptr;
   void (*on_destroy_device)(ID3D12Device* device) = nullptr;
   void (*on_create_swapchain)(SwapchainDesc& desc) = nullptr;
   void (*on_init_swapchain)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue, bool resize) = nullptr;
   void (*on_present)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) = nullptr;
};
} // namespace D3D12

// Replace the events set before for that API
void SetEvents(const D3D11::Events& events);
void SetEvents(const D3D12::Events& events);
} // namespace Game
} // namespace Luma
