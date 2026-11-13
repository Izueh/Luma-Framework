#pragma once

#include <Windows.h>
#include <d3d11_4.h>

namespace Luma {
namespace Game {
namespace D3D11 {
// Only for the functions no Core feature hooks (Startup::GetOwnedFunctions). Setting one installs its hook, so they are set in
// Init, in field order. They get the real API objects, not Luma's proxies. A Pre returning true skips the call.
struct Callbacks
{
   // Resources
   void (*pre_create_buffer)(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*post_create_buffer)(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer) = nullptr;
   void (*pre_create_texture_2d)(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*post_create_texture_2d)(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture) = nullptr;
   // Buffers and textures of every dimension
   void (*on_resource_destroyed)(ID3D11Device* device, const ID3D11Resource* resource) = nullptr;

   // Views: a null desc is the game's null one
   void (*post_create_shader_resource_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view) = nullptr;
   void (*post_create_render_target_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view) = nullptr;
   void (*post_create_unordered_access_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view) = nullptr;
   void (*post_create_depth_stencil_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view) = nullptr;
   void (*on_view_destroyed)(ID3D11Device* device, const ID3D11View* view) = nullptr;

   // States. CreateBlendState is reported as a DESC1 with logic ops disabled.
   void (*pre_create_sampler_state)(ID3D11Device* device, D3D11_SAMPLER_DESC& desc) = nullptr;
   void (*pre_create_blend_state)(ID3D11Device* device, D3D11_BLEND_DESC1& desc) = nullptr;

   // Binds: the arrays are copies of the game's, which can be edited. Null arrays when the game keeps the bound ones (D3D11_KEEP_*).
   bool (*pre_om_set_render_targets)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil) = nullptr;
   bool (*pre_om_set_render_targets_and_unordered_access_views)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
      ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts) = nullptr;
   void (*post_ia_set_vertex_buffers)(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) = nullptr;
   void (*post_ia_set_index_buffer)(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) = nullptr;

   // Data
   void (*post_map)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped) = nullptr;
   bool (*pre_unmap)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // After the original returned (the mapped memory is gone)
   void (*post_unmap)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // `copy_flags` is only set by UpdateSubresource1
   bool (*pre_update_subresource)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
      UINT row_pitch, UINT depth_pitch, UINT copy_flags) = nullptr;
   bool (*pre_copy_resource)(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) = nullptr;
   // A whole-subresource copy has no box, as the game passed it
   bool (*pre_copy_subresource_region)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
      ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags) = nullptr;
   bool (*pre_resolve_subresource)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
      UINT source_subresource, DXGI_FORMAT format) = nullptr;
   bool (*pre_clear_render_target_view)(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]) = nullptr;
   bool (*pre_copy_structure_count)(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source) = nullptr;
   // Not reported as a render target clear, even on a render target view
   bool (*pre_clear_view)(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count) = nullptr;

   // Deferred contexts
   void (*post_create_deferred_context)(ID3D11Device* device, ID3D11DeviceContext* context) = nullptr;
   void (*on_deferred_context_destroyed)(ID3D11Device* device, const ID3D11DeviceContext* context) = nullptr;
};
} // namespace D3D11

// Replaces the callbacks set before
void SetCallbacks(const D3D11::Callbacks& callbacks);
} // namespace Game
} // namespace Luma
