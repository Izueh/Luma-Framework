#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

#include <d3d12.h>

#include "Game/GameEvents.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"

namespace Luma {
namespace Handlers {
// Device and queue lifecycle
// Enables the debug layer for the device about to be created, when this build asks for it
void PreCreateDevice();
void PostCreateDevice(ID3D12Device* device);
// The game released its last reference. The device is still valid.
void OnDeviceDestroyed(ID3D12Device* device);
// Every submission, Luma's own included
void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

// Pipelines and root signatures
// Pipelines created on the device or loaded from the game's pipeline library: rewrites an embedded root signature to its extended one,
// invokes `call_original`, and tracks/attaches variants on the created pipeline.
HRESULT CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, bool from_library, void** pipeline,
   const std::function<HRESULT(const D3D12_GRAPHICS_PIPELINE_STATE_DESC*)>& call_original);
HRESULT CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, bool from_library, void** pipeline,
   const std::function<HRESULT(const D3D12_COMPUTE_PIPELINE_STATE_DESC*)>& call_original);
HRESULT CreatePipelineState(ID3D12Device* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, bool from_library, void** pipeline,
   const std::function<HRESULT(const D3D12_PIPELINE_STATE_STREAM_DESC*)>& call_original);
void PostCreatePipelineLibrary(size_t blob_size, HRESULT result);
// Extends the game's root signature with Luma's parameters, falls back to the game's blob if refused, and tracks the created signature.
HRESULT CreateRootSignature(std::span<const uint8_t> game_blob, void** root_signature,
   const std::function<HRESULT(std::span<const uint8_t>)>& call_original);
HRESULT CreateRootSignatureFromSubobjectInLibrary(std::span<const uint8_t> library, std::wstring_view subobject_name, void** root_signature,
   const std::function<HRESULT(std::span<const uint8_t>)>& create_from_blob, const std::function<HRESULT()>& call_original);
void PostCreateStateObject(ID3D12Device* device, const D3D12_STATE_OBJECT_DESC* desc, void* state_object);

// Descriptor heaps and views
// Extends shader-visible heaps with Luma's tail (retrying smaller sizes before falling back to the game's desc) and tracks the heap.
HRESULT CreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc, void** heap,
   const std::function<HRESULT(const D3D12_DESCRIPTOR_HEAP_DESC&)>& call_original);
// ID3D12DescriptorHeap::GetDesc: hides Luma's tail so the game sees the descriptor count it asked for
void PostGetDesc(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_DESC& desc);
// Before a view's creation: the format it gets (an in-place resource's), UNKNOWN for the game's
DXGI_FORMAT PreCreateView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, Resources::Descriptors::D3D12::Kind kind, ID3D12Resource* resource,
   DXGI_FORMAT format, bool has_desc);
// Every kind of view and samplers, with what was written
void PostCreateView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const Resources::Descriptors::D3D12::Descriptor& descriptor);
// Per pair of destination and source ranges of CopyDescriptors (split where either range ends), and CopyDescriptorsSimple
void PostCopyDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count,
   D3D12_DESCRIPTOR_HEAP_TYPE type);

// Resources and GetDesc
// Before a committed or placed resource's creation: true when its desc (and clear value's format) were upgraded in place
bool PreCreateCommittedResource(ID3D12Device* device, D3D12_RESOURCE_DESC& desc, D3D12_CLEAR_VALUE* clear, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags);
bool PreCreatePlacedResource(ID3D12Device* device, D3D12_RESOURCE_DESC& desc, D3D12_CLEAR_VALUE* clear, ID3D12Heap* heap, UINT64 offset);
// GetResourceAllocationInfo, per desc: true when it was upgraded in place
bool PreGetResourceAllocationInfo(ID3D12Device* device, D3D12_RESOURCE_DESC& desc);
// Committed, placed and reserved resources: `state` is the initial one, none for a creation with an enhanced barrier layout. A null
// `resource`: an upgraded creation failed, the game's is made next.
void PostCreateResource(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved);
// GetDesc and GetDesc1 of a resource, for the game: what it asked for when Luma upgraded it in place and the rules spoof it
void PostGetDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc);
void PostGetDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC1& desc);

// Command list lifecycle and pipeline binds (a pipeline argument can be replaced by its variant)
void PreReset(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
void PreClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
void PreSetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
// SetPipelineState1
void PreSetStateObject(ID3D12GraphicsCommandList* list);
void PreSetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
// The heaps Luma's table is read from
void PreSetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps);
// What trace reads a call's state from (Development builds hook them)
void PreSetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table);
void PreSetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address);
void PreSetRootConstants(ID3D12GraphicsCommandList* list, bool compute, UINT index, UINT count, const void* values, UINT offset);

// Render targets and rasterizer/OM state
// The handles can be replaced (mirrors' render targets)
void PreOMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil);
void PreSetViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports);
void PreSetScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects);
void PreSetTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology);
void PreSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4]);
void PreSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref);

// Barriers, copies and clears (true skips the call; locations are copies of the game's, which can be edited)
// The barriers to issue: the game's, with the mirrors' appended
std::span<const D3D12_RESOURCE_BARRIER> PreResourceBarrier(ID3D12GraphicsCommandList* list, std::span<const D3D12_RESOURCE_BARRIER> barriers);
bool PreCopyResource(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source);
bool PreCopyTextureRegion(ID3D12GraphicsCommandList* list, D3D12_TEXTURE_COPY_LOCATION& destination, UINT& x, UINT& y, UINT& z, D3D12_TEXTURE_COPY_LOCATION& source,
   const D3D12_BOX*& box);
bool PreCopyBufferRegion(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, UINT64 destination_offset, ID3D12Resource* source, UINT64 source_offset,
   UINT64 size);
bool PreResolveSubresource(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, UINT destination_subresource, ID3D12Resource*& source,
   UINT source_subresource, DXGI_FORMAT& format);
bool PreClearRenderTargetView(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE& view, const FLOAT color[4], UINT rect_count, const D3D12_RECT*& rects);
bool PreClearDepthStencilView(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE& view, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT rect_count,
   const D3D12_RECT*& rects);

// One of the two is set: ClearUnorderedAccessViewUint's or ClearUnorderedAccessViewFloat's
struct ClearValues
{
   const UINT* uint_values = nullptr;
   const FLOAT* float_values = nullptr;
};

bool PreClearUnorderedAccessView(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource,
   ClearValues values, UINT rect_count, const D3D12_RECT* rects);

// Draws, dispatches and bundles (true when the game's call doesn't run)
bool PreDraw(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw);
void PostDraw(ID3D12GraphicsCommandList* list);
bool PreDispatch(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch);
void PostDispatch(ID3D12GraphicsCommandList* list);
// A draw or a dispatch by the pipeline bound
bool PreExecuteIndirect(ID3D12GraphicsCommandList* list, const Game::D3D12::IndirectArguments& arguments);
void PostExecuteIndirect(ID3D12GraphicsCommandList* list);
void PreExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle);

// Present (presenting thread, at a D3D12 present)
void PresentD3D12(ID3D12CommandQueue* queue);
} // namespace Handlers
} // namespace Luma
