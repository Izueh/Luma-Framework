#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <d3d12.h>

#include "Game/Events.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"

namespace Luma {
namespace Handlers {
// Enables the debug layer for the device about to be created, when this build asks for it
void PreCreateDevice();
void PostCreateDevice(ID3D12Device* device);
// The game released its last reference. The device is still valid.
void OnDeviceDestroyed(ID3D12Device* device);
// Every submission, Luma's own included
void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

// Pipelines the device creates or the game's pipeline library loads. `desc` is of `kind`; the Pre handler can replace it (a root
// signature that comes with the pipeline, for its extended one), the Post one gets the desc used.
std::optional<Shaders::RootSignatures::D3D12::RewrittenDesc> PreCreatePipelineState(ID3D12Device* device, Shaders::Database::D3D12::PipelineKind kind,
   const void* desc);
void PostCreatePipelineState(Shaders::Database::D3D12::PipelineKind kind, const void* desc, bool from_library, ID3D12PipelineState* pipeline);
// Any load from the game's pipeline library, before PostCreatePipelineState for one that succeeded
void PostLoadPipeline(HRESULT result);
void PostCreatePipelineLibrary(size_t blob_size, HRESULT result);
// The game's signature extended with Luma's parameters, empty to create the game's own
Shaders::RootSignatures::D3D12::Extended PreCreateRootSignature(std::span<const uint8_t> blob);
// The device refused the extended signature: the game's own is created
void PostExtendedRootSignatureRefused(HRESULT result);
// `luma_parameter` is UINT32_MAX for the game's own signature
void PostCreateRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter);
void PostCreateRootSignatureFromLibrary();
// CreateStateObject and AddToStateObject
void PostCreateStateObject();
void PostCreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc, ID3D12DescriptorHeap* heap);
// Every kind of view and samplers, with what was written
void PostCreateView(D3D12_CPU_DESCRIPTOR_HANDLE destination, const Resources::Descriptors::D3D12::Descriptor& descriptor);
// Per pair of destination and source ranges of CopyDescriptors (split where either range ends), and CopyDescriptorsSimple
void PostCopyDescriptors(D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count);
// Committed, placed and reserved resources
void PostCreateResource(ID3D12Resource* resource);

// Command lists. A pipeline argument can be replaced by its variant.
void PreReset(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
void PreClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
void PreSetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
// SetPipelineState1
void PreSetStateObject(ID3D12GraphicsCommandList* list);
void PreSetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
void PreOMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil);
// What trace reads a call's state from (Development builds hook them)
void PreSetDescriptorHeaps(ID3D12GraphicsCommandList* list);
void PreSetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table);
void PreSetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address);
void PreSetRootConstants(ID3D12GraphicsCommandList* list, bool compute, UINT index, UINT count, const void* values, UINT offset);
void PreSetViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports);
void PreSetScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects);
void PreSetTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology);
void PreSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4]);
void PreSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref);
// DrawInstanced, DrawIndexedInstanced, DispatchMesh, and Dispatch: true when the game's call doesn't run (a draw event or the
// trace skipped or replaced it)
bool PreDraw(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw);
bool PreDispatch(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch);
// A draw or a dispatch by the pipeline bound
bool PreExecuteIndirect(ID3D12GraphicsCommandList* list, const Game::D3D12::IndirectArguments& arguments);
void PreExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle);

// Presenting thread, at a D3D12 present
void PresentD3D12();
} // namespace Handlers
} // namespace Luma
