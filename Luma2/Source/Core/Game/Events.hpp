#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include "Common/ShaderStage.hpp"
#include "Hooks/Functions.hpp"
#include "Shaders/Stages.hpp"

namespace Luma {
namespace Game {
enum class DrawResult : uint8_t
{
   None, // The game's call runs, with what the callback changed
   Skip, // The game's call doesn't run
   Replaced, // The callback drew in its place (RunOriginal draws the game's call with other bindings): the game's call doesn't run
};

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
// The arguments `function` doesn't take are zero
struct DrawCall
{
   Hooks::D3D11::Function function;
   UINT vertex_or_index_count = 0;
   UINT instance_count = 0;
   UINT start_vertex_or_index = 0;
   INT base_vertex = 0;
   UINT start_instance = 0;
   ID3D11Buffer* arguments = nullptr; // Indirect draws
   UINT arguments_offset = 0;
};

struct DispatchCall
{
   UINT x = 0;
   UINT y = 0;
   UINT z = 0;
   ID3D11Buffer* arguments = nullptr; // DispatchIndirect
   UINT arguments_offset = 0;
};

// A draw or a dispatch of the game, as its draw events get it
struct Call
{
   ID3D11DeviceContext* context = nullptr;
   const DrawCall* draw = nullptr; // Null for a dispatch
   const DispatchCall* dispatch = nullptr; // Null for a draw
   // The game's shader bound per stage (ShaderStage order), 0 where none is or for one Luma didn't see created
   std::array<uint32_t, 6> hashes = {};
   // The stages running a variant of the game's shader
   Luma::Shaders::StageSet variant_stages;
};

using DrawCallback = DrawResult (*)(const Call& call);

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

   // Draws and dispatches, before the game's call and before the variants' requests are bound (they're bound after the events, only
   // when they all return None, 1.0). Their result can skip the call or replace it. Their own calls aren't hooked or traced, so they
   // restore what later draws would see. None while Luma is off.
   // 1.0's OnDrawOrDispatchCustomPipeline: runs first, only for a call with a variant bound (`variant_stages`). The other events run
   // only if it returns None.
   DrawResult (*on_variant_draw)(const Call& call) = nullptr;
   // 1.0's OnDrawOrDispatch: for each call no shader's callback (OnShaderDraw) took
   DrawResult (*on_draw_or_dispatch)(const Call& call) = nullptr;

   // After Core's handling
   void (*on_finish_command_list)(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list) = nullptr;
   // Before Core's handling (1.0), so the game can run its own lists first. True skips the execution.
   bool (*on_execute_command_list)(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore) = nullptr;
};

// 1.0's draw_callbacks_by_shader_hashes, a list so a call: `callback` runs in place of on_draw_or_dispatch for a call with any of
// `hashes` bound, on any stage (pixel first, then vertex, geometry, hull, domain, compute). One callback per hash, the last set wins.
void OnShaderDraw(std::span<const uint32_t> hashes, DrawCallback callback);
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
