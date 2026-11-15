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

   // Resources and views (Core's mirrors and trace hook them). As 1.0's: an on_create_* runs before Core and may edit the desc (Core
   // sees the edit), an on_init_* after Core with the created object. Objects are always the game's, never Luma's mirrors.
   void (*on_create_buffer)(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*on_init_buffer)(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer) = nullptr;
   void (*on_create_texture_2d)(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*on_init_texture_2d)(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture) = nullptr;
   // Buffers and textures of every dimension
   void (*on_resource_destroyed)(ID3D11Device* device, const ID3D11Resource* resource) = nullptr;
   // A null desc is the game's null one
   void (*on_init_shader_resource_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view) = nullptr;
   void (*on_init_render_target_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view) = nullptr;
   void (*on_init_unordered_access_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view) = nullptr;
   void (*on_init_depth_stencil_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view) = nullptr;
   void (*on_view_destroyed)(ID3D11Device* device, const ID3D11View* view) = nullptr;
   // Before Core's sampler mirror: may edit the desc
   void (*on_create_sampler_state)(ID3D11Device* device, D3D11_SAMPLER_DESC& desc) = nullptr;

   // Render target binds, before Core swaps in mirrors: may edit the arrays (copies of the game's; null when the game keeps the bound
   // ones, D3D11_KEEP_*). True skips the bind.
   bool (*on_bind_render_targets)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil) = nullptr;
   bool (*on_bind_render_targets_and_unordered_access_views)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
      ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts) = nullptr;

   // CPU writes, copies and clears, before Core redirects them to mirrors (on_map and on_unmapped after the call). True skips the
   // call.
   void (*on_map)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped) = nullptr;
   bool (*on_unmap)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // After the original returned (the mapped memory is gone)
   void (*on_unmapped)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // `copy_flags` is only set by UpdateSubresource1
   bool (*on_update_subresource)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
      UINT row_pitch, UINT depth_pitch, UINT copy_flags) = nullptr;
   bool (*on_copy_resource)(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) = nullptr;
   // A whole-subresource copy has no box, as the game passed it
   bool (*on_copy_subresource_region)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
      ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags) = nullptr;
   bool (*on_resolve_subresource)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
      UINT source_subresource, DXGI_FORMAT format) = nullptr;
   bool (*on_clear_render_target_view)(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]) = nullptr;
   // Not reported as a render target clear, even on a render target view
   bool (*on_clear_view)(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count) = nullptr;

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
// The command list call: its arguments, the others zero
enum class DrawFunction : uint8_t
{
   DrawInstanced,
   DrawIndexedInstanced,
   DispatchMesh,
   ExecuteIndirect, // Draws of a graphics pipeline
};

struct IndirectArguments
{
   ID3D12CommandSignature* signature = nullptr;
   UINT max_count = 0;
   ID3D12Resource* arguments = nullptr;
   UINT64 arguments_offset = 0;
   ID3D12Resource* count_buffer = nullptr;
   UINT64 count_offset = 0;
};

struct DrawCall
{
   DrawFunction function = DrawFunction::DrawInstanced;
   UINT vertex_or_index_count = 0;
   UINT instance_count = 0;
   UINT start_vertex_or_index = 0;
   INT base_vertex = 0;
   UINT start_instance = 0;
   std::array<UINT, 3> mesh_groups = {}; // DispatchMesh
   IndirectArguments indirect;
};

struct DispatchCall
{
   UINT x = 0;
   UINT y = 0;
   UINT z = 0;
   bool indirect_call = false; // ExecuteIndirect with a compute pipeline bound
   IndirectArguments indirect;
};

// A draw or a dispatch of the game, as its draw events get it. D3D12 bakes the shaders into a pipeline: `pipeline` is the game's
// (bound by the game, the variant runs in its place), null for a state object or one Luma didn't see created.
struct Call
{
   ID3D12GraphicsCommandList* list = nullptr;
   const DrawCall* draw = nullptr; // Null for a dispatch
   const DispatchCall* dispatch = nullptr; // Null for a draw
   ID3D12PipelineState* pipeline = nullptr;
   // The game's shader per stage (ShaderStage order), 0 where the pipeline has none
   std::array<uint32_t, 6> hashes = {};
   // The stages running a variant of the game's shader: the variant pipeline is bound
   Luma::Shaders::StageSet variant_stages;
};

using DrawCallback = DrawResult (*)(const Call& call);

// As D3D11's: each runs where 1.0's Core called the game, set in Init, with the real API objects
struct Events
{
   void (*on_init_device)(ID3D12Device* device) = nullptr;
   void (*on_destroy_device)(ID3D12Device* device) = nullptr;
   void (*on_create_swapchain)(SwapchainDesc& desc) = nullptr;
   void (*on_init_swapchain)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue, bool resize) = nullptr;
   void (*on_present)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) = nullptr;

   // Draws, dispatches and ExecuteIndirect on the game's command lists (bundles included), as D3D11's: before the game's call,
   // the variants' requests bound after them when they all return None. Their own list calls aren't hooked or traced. None while
   // Luma is off.
   DrawResult (*on_variant_draw)(const Call& call) = nullptr;
   DrawResult (*on_draw_or_dispatch)(const Call& call) = nullptr;
};

// As D3D11's OnShaderDraw: `callback` runs in place of on_draw_or_dispatch for a call whose pipeline has any of `hashes`
void OnShaderDraw(std::span<const uint32_t> hashes, DrawCallback callback);
} // namespace D3D12

// Replace the events set before for that API
void SetEvents(const D3D11::Events& events);
void SetEvents(const D3D12::Events& events);
} // namespace Game
} // namespace Luma
