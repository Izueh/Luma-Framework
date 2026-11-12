#include "Handlers/D3D11Handlers.hpp"

#include "Game/Game.hpp"
#include "Handlers/DXGIHandlers.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Validation/D3D11Validation.hpp"

// No Core feature uses these yet: each forwards to the game's callback, when the settled game API has one
namespace Luma {
namespace Handlers {
namespace {
const Game::D3D11::Callbacks& GameCallbacks()
{
   return Game::GetD3D11();
}
} // namespace

void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count)
{
   (void)feature_levels;
   (void)feature_level_count;
   flags = Validation::D3D11::GetCreateFlags(flags);
}

void PostCreateDevice(ID3D11Device* device)
{
   Validation::D3D11::OnDeviceCreated(device);
   Utils::Log::Info("D3D11: device {} created (feature level 0x{:X}, {} live)", static_cast<void*>(device), uint32_t(device->GetFeatureLevel()),
      Hooks::DeviceProxy::GetCount());
   Game::CreateDeviceData(device);
   Game::Call(GameCallbacks().post_create_device, device);
}

void OnDeviceDestroyed(ID3D11Device* device)
{
   Utils::Log::Info("D3D11: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Call(GameCallbacks().on_device_destroyed, device);
   UI::OnDeviceReleased(device);
   ForgetSwapchains(device);
   Validation::D3D11::OnDeviceReleased(device);
}

void PreCreateBuffer(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Call(GameCallbacks().pre_create_buffer, device, desc, initial_data);
}

void PostCreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer)
{
   Game::Call(GameCallbacks().post_create_buffer, device, desc, initial_data, buffer);
}

void PreCreateTexture1D(ID3D11Device*, D3D11_TEXTURE1D_DESC&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture1D(ID3D11Device*, const D3D11_TEXTURE1D_DESC&, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D*) {}

void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   Game::Call(GameCallbacks().pre_create_texture_2d, device, desc, initial_data);
}

void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   Game::Call(GameCallbacks().post_create_texture_2d, device, desc, initial_data, texture);
}

void PreCreateTexture3D(ID3D11Device*, D3D11_TEXTURE3D_DESC1&, const D3D11_SUBRESOURCE_DATA*&) {}

void PostCreateTexture3D(ID3D11Device*, const D3D11_TEXTURE3D_DESC1&, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D*) {}

void PostOpenSharedResource(ID3D11Device*, ID3D11Resource*) {}

void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource)
{
   Game::Call(GameCallbacks().on_resource_destroyed, device, resource);
}

void PreCreateShaderResourceView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>&) {}

void PostCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view)
{
   Game::Call(GameCallbacks().post_create_shader_resource_view, device, resource, desc, view);
}

void PreCreateRenderTargetView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>&) {}

void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view)
{
   Game::Call(GameCallbacks().post_create_render_target_view, device, resource, desc, view);
}

void PreCreateUnorderedAccessView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>&) {}

void PostCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view)
{
   Game::Call(GameCallbacks().post_create_unordered_access_view, device, resource, desc, view);
}

void PreCreateDepthStencilView(ID3D11Device*, ID3D11Resource*, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>&) {}

void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view)
{
   Game::Call(GameCallbacks().post_create_depth_stencil_view, device, resource, desc, view);
}

void OnViewDestroyed(ID3D11Device* device, const ID3D11View* view)
{
   Game::Call(GameCallbacks().on_view_destroyed, device, view);
}

void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc)
{
   Game::Call(GameCallbacks().pre_create_sampler_state, device, desc);
}

void PostCreateSamplerState(ID3D11Device*, const D3D11_SAMPLER_DESC&, ID3D11SamplerState*) {}

void OnSamplerDestroyed(ID3D11Device*, const ID3D11SamplerState*) {}

void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc)
{
   Game::Call(GameCallbacks().pre_create_blend_state, device, desc);
}

void PostCreateBlendState(ID3D11Device*, const D3D11_BLEND_DESC1&, ID3D11BlendState*) {}

void PreCreateShader(ID3D11Device* device, ShaderStage stage, const void*& bytecode, SIZE_T& length)
{
   Game::Call(GameCallbacks().pre_create_shader, device, stage, bytecode, length);
}

void PostCreateShader(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, ID3D11DeviceChild* shader)
{
   Game::Call(GameCallbacks().post_create_shader, device, stage, bytecode, length, shader);
}

void OnShaderDestroyed(ID3D11Device*, const ID3D11DeviceChild*) {}

void PostCreateInputLayout(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout*) {}

void OnInputLayoutDestroyed(ID3D11Device*, const ID3D11InputLayout*) {}

bool PreSetShader(ID3D11DeviceContext*, ShaderStage, ID3D11DeviceChild*&)
{
   return false;
}

bool PreSetShaderResources(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11ShaderResourceView**)
{
   return false;
}

bool PreSetConstantBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant,
   const UINT* num_constants)
{
   return Game::Call(GameCallbacks().pre_set_constant_buffers, context, stage, start, count, buffers, first_constant, num_constants);
}

bool PreSetSamplers(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11SamplerState**)
{
   return false;
}

bool PreCSSetUnorderedAccessViews(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView**, const UINT*)
{
   return false;
}

bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil)
{
   return Game::Call(GameCallbacks().pre_om_set_render_targets, context, count, views, depth_stencil);
}

bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
   ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts)
{
   return Game::Call(GameCallbacks().pre_om_set_render_targets_and_unordered_access_views, context, count, views, depth_stencil, uav_start, uav_count, uavs,
      initial_counts);
}

void PostRSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   Game::Call(GameCallbacks().post_rs_set_viewports, context, count, viewports);
}

void PostRSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   Game::Call(GameCallbacks().post_rs_set_scissor_rects, context, count, rects);
}

void PostOMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask)
{
   Game::Call(GameCallbacks().post_om_set_blend_state, context, state, blend_factor, sample_mask);
}

void PostOMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref)
{
   Game::Call(GameCallbacks().post_om_set_depth_stencil_state, context, state, stencil_ref);
}

void PostIASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets)
{
   Game::Call(GameCallbacks().post_ia_set_vertex_buffers, context, start, count, buffers, strides, offsets);
}

void PostIASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset)
{
   Game::Call(GameCallbacks().post_ia_set_index_buffer, context, buffer, format, offset);
}

void PostClearState(ID3D11DeviceContext* context)
{
   Game::Call(GameCallbacks().post_clear_state, context);
}

void PostMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped)
{
   Game::Call(GameCallbacks().post_map, context, resource, subresource, type, flags, mapped);
}

bool PreUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   return Game::Call(GameCallbacks().pre_unmap, context, resource, subresource);
}

void PostUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   Game::Call(GameCallbacks().post_unmap, context, resource, subresource);
}

bool PreUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch, UINT copy_flags)
{
   return Game::Call(GameCallbacks().pre_update_subresource, context, resource, subresource, box, data, row_pitch, depth_pitch, copy_flags);
}

bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   return Game::Call(GameCallbacks().pre_copy_resource, context, destination, source);
}

bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   return Game::Call(GameCallbacks().pre_copy_subresource_region, context, destination, destination_subresource, x, y, z, source, source_subresource, box,
      copy_flags);
}

bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   return Game::Call(GameCallbacks().pre_resolve_subresource, context, destination, destination_subresource, source, source_subresource, format);
}

bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   return Game::Call(GameCallbacks().pre_clear_render_target_view, context, view, color);
}

bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source)
{
   return Game::Call(GameCallbacks().pre_copy_structure_count, context, destination, destination_offset, source);
}

bool PreClearDepthStencilView(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8)
{
   return false;
}

bool PreClearUnorderedAccessViewUint(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const UINT[4])
{
   return false;
}

bool PreClearUnorderedAccessViewFloat(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const FLOAT[4])
{
   return false;
}

bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   return Game::Call(GameCallbacks().pre_clear_view, context, view, color, rects, rect_count);
}

bool PreDraw(ID3D11DeviceContext*, const DrawCall&)
{
   return false;
}

bool PreDispatch(ID3D11DeviceContext*, const DispatchCall&)
{
   return false;
}

void PostCreateDeferredContext(ID3D11Device* device, ID3D11DeviceContext* context)
{
   Game::Call(GameCallbacks().post_create_deferred_context, device, context);
}

void OnDeferredContextDestroyed(ID3D11Device* device, const ID3D11DeviceContext* context)
{
   Game::Call(GameCallbacks().on_deferred_context_destroyed, device, context);
}

void PostFinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list)
{
   Game::Call(GameCallbacks().post_finish_command_list, context, restore, list);
}

void OnCommandListDestroyed(ID3D11Device*, const ID3D11CommandList*) {}

bool PreExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore)
{
   return Game::Call(GameCallbacks().pre_execute_command_list, context, list, restore);
}

bool PreFlush(ID3D11DeviceContext*)
{
   return false;
}
} // namespace Handlers
} // namespace Luma
