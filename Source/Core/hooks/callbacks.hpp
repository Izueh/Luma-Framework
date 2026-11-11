#pragma once

#include <optional>

#include <d3d11_4.h>
#include <dxgi1_4.h>

// Defined by Core. Each runs inside a Luma scope. A "Pre" callback runs before the original and can edit its arguments
// (a context one returns true to skip the original), a "Post" one after it succeeded. "On*Destroyed" receive an object that
// can't be used anymore.
namespace Luma {
namespace Callbacks {
void PreCreateDevice(UINT& flags, const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count);
void PostCreateDevice(ID3D11Device* device);
// The game released its last reference, the device is still valid
void OnDeviceDestroyed(ID3D11Device* device);

void PreCreateSwapChain(DXGI_SWAP_CHAIN_DESC& desc);
void PreCreateSwapChainForHwnd(DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd);
void PostCreateSwapChain(IDXGISwapChain* swapchain, ID3D11Device* device);
void OnSwapChainDestroyed(ID3D11Device* device, const IDXGISwapChain* swapchain);

void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT& flags,
   const UINT* node_masks = nullptr, IUnknown* const* present_queues = nullptr);
void PostResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
// True skips the call
bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output);

void PrePresent(IDXGISwapChain* swapchain);
void PostPresent(IDXGISwapChain* swapchain);

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
// "CreateBlendState" reports the DESC1 with logic ops disabled
void PreCreateBlendState(ID3D11Device* device, D3D11_BLEND_DESC1& desc);
void PostCreateBlendState(ID3D11Device* device, const D3D11_BLEND_DESC1& desc, ID3D11BlendState* blend_state);
void OnSamplerDestroyed(ID3D11Device* device, const ID3D11SamplerState* sampler);

// Shaders
void PreCreateVertexShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateVertexShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11VertexShader* shader);
void PreCreateHullShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateHullShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11HullShader* shader);
void PreCreateDomainShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateDomainShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11DomainShader* shader);
void PreCreateGeometryShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateGeometryShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11GeometryShader* shader);
void PreCreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateGeometryShaderWithStreamOutput(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11GeometryShader* shader);
void PreCreatePixelShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreatePixelShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11PixelShader* shader);
void PreCreateComputeShader(ID3D11Device* device, const void*& bytecode, SIZE_T& length);
void PostCreateComputeShader(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ComputeShader* shader);
void OnShaderDestroyed(ID3D11Device* device, const ID3D11DeviceChild* shader);
void PostCreateInputLayout(ID3D11Device* device, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* bytecode, SIZE_T length,
   ID3D11InputLayout* layout);
void OnInputLayoutDestroyed(ID3D11Device* device, const ID3D11InputLayout* layout);

// Binds: the arrays are the detour's copies, which can be edited
bool PreVSSetShader(ID3D11DeviceContext* context, ID3D11VertexShader*& shader);
bool PreHSSetShader(ID3D11DeviceContext* context, ID3D11HullShader*& shader);
bool PreDSSetShader(ID3D11DeviceContext* context, ID3D11DomainShader*& shader);
bool PreGSSetShader(ID3D11DeviceContext* context, ID3D11GeometryShader*& shader);
bool PrePSSetShader(ID3D11DeviceContext* context, ID3D11PixelShader*& shader);
bool PreCSSetShader(ID3D11DeviceContext* context, ID3D11ComputeShader*& shader);

bool PreVSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);
bool PreHSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);
bool PreDSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);
bool PreGSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);
bool PrePSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);
bool PreCSSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView** views);

bool PreVSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);
bool PreHSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);
bool PreDSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);
bool PreGSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);
bool PrePSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);
bool PreCSSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers,
   const UINT* first_constant = nullptr, const UINT* num_constants = nullptr);

bool PreVSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PreHSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PreDSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PreGSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PrePSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);
bool PreCSSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState** samplers);

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
   UINT row_pitch, UINT depth_pitch, UINT copy_flags = 0);
bool PreCopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source);
bool PreCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags = 0);
bool PreResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format);
bool PreClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]);
bool PreCopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* dest, UINT dest_offset, ID3D11UnorderedAccessView* source);
bool PreClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil);
bool PreClearUnorderedAccessViewUint(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4]);
bool PreClearUnorderedAccessViewFloat(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4]);
bool PreClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count);

// Draws and dispatches
bool PreDraw(ID3D11DeviceContext* context, UINT vertex_count, UINT start_vertex);
bool PreDrawInstanced(ID3D11DeviceContext* context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance);
bool PreDrawIndexed(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex);
bool PreDrawIndexedInstanced(ID3D11DeviceContext* context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance);
bool PreDrawAuto(ID3D11DeviceContext* context);
bool PreDrawInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset);
bool PreDrawIndexedInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset);
bool PreDispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z);
bool PreDispatchIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset);

// Deferred contexts
void PostCreateDeferredContext(ID3D11Device* device, ID3D11DeviceContext* context);
void OnDeferredContextDestroyed(ID3D11Device* device, const ID3D11DeviceContext* context);
void PostFinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list);
void OnCommandListDestroyed(ID3D11Device* device, const ID3D11CommandList* list);
bool PreExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore);
bool PreFlush(ID3D11DeviceContext* context);
} // namespace Callbacks
} // namespace Luma
