#pragma once

#include <optional>

#include <d3d11_4.h>

#include "Common/Api.hpp"
#include "Hooks/Functions.hpp"

// One handler per hooked native function, called by its detour inside a Luma scope. A "Pre" handler runs before the original
// and can edit its arguments (true skips the original), a "Post" one after it succeeded. "On*Destroyed" ones receive an object
// that can't be used anymore. Each runs the game's callback and Core's features: in a "Pre" the game first, in a "Post" Core first.
namespace Luma {
namespace Handlers {
// Any of the seven draw functions, with the arguments it has (the others are zero)
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

void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count);
void PostCreateDevice(ID3D11Device* device);
// The game released its last reference, the device is still valid
void OnDeviceDestroyed(ID3D11Device* device);

// Resources
void PreCreateBuffer(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data);
void PostCreateBuffer(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer);
void PreCreateTexture1D(ID3D11Device* device, D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data);
void PostCreateTexture1D(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture);
void PreCreateTexture2D(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data);
void PostCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture);
void PreCreateTexture3D(ID3D11Device* device, D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data);
void PostCreateTexture3D(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture);
void PostOpenSharedResource(ID3D11Device* device, ID3D11Resource* resource);
void OnResourceDestroyed(ID3D11Device* device, const ID3D11Resource* resource);

// Views: an empty desc is the game's null one
void PreCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>& desc);
void PostCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view);
void PreCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>& desc);
void PostCreateRenderTargetView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view);
void PreCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>& desc);
void PostCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view);
void PreCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>& desc);
void PostCreateDepthStencilView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view);
void OnViewDestroyed(ID3D11Device* device, const ID3D11View* view);

void PreCreateSamplerState(ID3D11Device* device, D3D11_SAMPLER_DESC& desc);
void PostCreateSamplerState(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler);
void OnSamplerDestroyed(ID3D11Device* device, const ID3D11SamplerState* sampler);
// CreateBlendState is reported as a DESC1 with logic ops disabled
void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc);
void PostCreateBlendState(ID3D11Device* device, const D3D11_BLEND_DESC1& desc, ID3D11BlendState* blend_state);

// Shaders of every stage, geometry shaders with stream output included
void PreCreateShader(ID3D11Device* device, ShaderStage stage, const void*& bytecode, SIZE_T& length);
void PostCreateShader(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, ID3D11DeviceChild* shader);
void OnShaderDestroyed(ID3D11Device* device, const ID3D11DeviceChild* shader);
void PostCreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* bytecode, SIZE_T length,
   ID3D11InputLayout* layout);
void OnInputLayoutDestroyed(ID3D11Device* device, const ID3D11InputLayout* layout);

// Binds: the arrays are the detour's copies, which can be edited
bool PreSetShader(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild*& shader);
bool PreSetShaderResources(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11ShaderResourceView** views);
// `first_constant` and `num_constants` are only set by the *SetConstantBuffers1 functions
bool PreSetConstantBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers, const UINT* first_constant,
   const UINT* num_constants);
bool PreSetSamplers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PreCSSetUnorderedAccessViews(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11UnorderedAccessView** views, const UINT* initial_counts);
// Null arrays when the game keeps the bound ones (D3D11_KEEP_*)
bool PreOMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil);
bool PreOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
   ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts);
void PostRSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports);
void PostRSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects);
void PostOMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask);
void PostOMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref);
void PostIASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets);
void PostIASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset);
void PostClearState(ID3D11DeviceContext* context);

// Data operations
void PostMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped);
bool PreUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource);
// After the original returned (the mapped memory is gone)
void PostUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource);
bool PreUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch, UINT copy_flags);
bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source);
bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags);
bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format);
bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]);
bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source);
bool PreClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil);
bool PreClearUnorderedAccessViewUint(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4]);
bool PreClearUnorderedAccessViewFloat(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4]);
bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count);

bool PreDraw(ID3D11DeviceContext* context, const DrawCall& draw);
bool PreDispatch(ID3D11DeviceContext* context, const DispatchCall& dispatch);

// Deferred contexts
void PostCreateDeferredContext(ID3D11Device* device, ID3D11DeviceContext* context);
void OnDeferredContextDestroyed(ID3D11Device* device, const ID3D11DeviceContext* context);
void PostFinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list);
void OnCommandListDestroyed(ID3D11Device* device, const ID3D11CommandList* list);
bool PreExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore);
bool PreFlush(ID3D11DeviceContext* context);
} // namespace Handlers
} // namespace Luma
