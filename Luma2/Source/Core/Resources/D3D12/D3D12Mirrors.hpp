#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include <d3d12.h>

#include "Shaders/D3D12/D3D12Database.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
// `state` is nullopt for enhanced-barrier resources
void CreateMirror(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved);
ID3D12Resource* FindMirror(ID3D12Resource* resource);
ID3D12Resource* FindOriginal(ID3D12Resource* mirror);
// The game released the device: Luma's descriptor heaps on it go
void ReleaseDeviceObjects(ID3D12Device* device);
// Whether any mirror or mirrored descriptor exists: nothing on the hot paths does anything until then
bool HasMirrors();

// A heap the game created: shader-visible ones are where copies are swapped
void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc);
enum class ViewKind : uint8_t
{
   ConstantBuffer,
   ShaderResource,
   UnorderedAccess,
   RenderTarget,
   DepthStencil,
};
// The game wrote a view at `destination`: one of a mirrored texture gets the mirror's, any other forgets what the handle had. `swapping`
// (Mirrors::IsSwapping) is for a view written straight into a shader-visible heap.
void TrackView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping);
// The same for samplers, `swapping` being Samplers::IsSwapping
void TrackSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping);
// After the game's copy of `count` descriptors (one run of CopyDescriptors): the destination handles hold what the sources did, and a
// shader-visible destination gets the mirrors' views (`swap_views`, Mirrors::IsSwapping) and samplers (`swap_samplers`)
void SwapCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   bool swap_views, bool swap_samplers);

// The game's barriers with the mirrors' appended: `barriers` itself when none is mirrored, else storage of the calling thread valid
// until its next call
std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(std::span<const D3D12_RESOURCE_BARRIER> barriers);

void RecordRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature);
void RecordRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil);
void RecordViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports);
void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects);
void RecordTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology);
void ForgetListState(ID3D12GraphicsCommandList* list);

// Swapped handles point into thread-local storage valid until the next call
void SwapRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range, const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil,
   Shaders::Database::D3D12::TargetFormats& formats);
bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle);

struct CopyRegion
{
   bool convertible = false;
   bool whole_resource = false;
   UINT destination_subresource = 0;
   UINT source_subresource = 0;
   std::array<UINT, 3> offset = {};
   const D3D12_BOX* box = nullptr;
};

// Returns true when Luma's copy pass replaced the game's call
bool SwapCopy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, const CopyRegion& region);
void SwapClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource);

struct Stats
{
   uint32_t descriptors = 0;
   uint32_t samplers = 0;
   uint32_t swapped_copies = 0;
   uint32_t direct_writes = 0;
   uint32_t converted_copies = 0;
   uint32_t unhandled_copies = 0;
   uint32_t uav_clears = 0;
   uint32_t unhandled_clears = 0;
   uint32_t layout_resources = 0;
   uint32_t reserved_resources = 0;
   uint32_t aliasing_barriers = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
