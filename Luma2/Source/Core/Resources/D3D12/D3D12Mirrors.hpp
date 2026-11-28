#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <d3d12.h>

#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
struct MirrorSummary
{
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo mirror_info;
   Chains::Origin origin = {};
   bool is_scaled = false;
   bool pending = false;
};

struct ListScalingSnapshot
{
   bool viewports_scaled = false;
   bool scissors_scaled = false;
   UINT game_viewport_count = 0;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_viewports = {};
   UINT game_scissor_count = 0;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_scissors = {};
   bool chain_seed = false;
   bool chain_propagated = false;
   bool cross_thread_overlap = false;
};

enum class ViewKind : uint8_t
{
   ConstantBuffer,
   ShaderResource,
   UnorderedAccess,
   RenderTarget,
   DepthStencil,
};

// Upgrade::Method::InPlace: the game's committed and placed textures created upgraded
enum class Placement : uint8_t
{
   Committed,
   Placed,
};

struct Creation
{
   Placement placement = Placement::Committed;
   // Committed
   D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
   D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
   // Placed
   ID3D12Heap* heap = nullptr;
   UINT64 offset = 0;
};

struct DescriptorSwapOptions
{
   bool views = false;
   bool samplers = false;
};

struct CopyRegion
{
   bool convertible = false;
   bool whole_resource = false;
   UINT destination_subresource = 0;
   UINT source_subresource = 0;
   std::array<UINT, 3> offset = {};
   const D3D12_BOX* box = nullptr;
};

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
   // InPlace: copies converted or scaled, copies skipped (a partial region, a copy list, a texture that can't be a target), placed
   // resources that got a mirror because the game never asked their size or the upgraded one didn't fit
   uint32_t in_place_converted_copies = 0;
   uint32_t in_place_unhandled_copies = 0;
   uint32_t in_place_unasked_placed = 0;
   uint32_t in_place_unfit_placed = 0;
};

// Core, resource creation and lifecycle (D3D12Mirrors.cpp)
// Before the game's creation: a texture the rules upgrade in place gets its desc (and its clear value's format) upgraded. A placed one only
// with the upgrade GetResourceAllocationInfo reported to the game for its desc, and if the upgraded size fits its heap from its offset.
bool PrepareResource(ID3D12Device* device, D3D12_RESOURCE_DESC& desc, D3D12_CLEAR_VALUE* clear, const Creation& creation);
// After it: the resource prepared on this thread is marked as upgraded in place, any other gets a mirror when the rules match it. A null
// resource: the creation failed (an upgraded one is then made as the game asked). `state` is none for an enhanced barrier layout.
void UpgradeResource(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved);
// GetResourceAllocationInfo: a desc the rules upgrade in place, upgraded (the game's allocator leaves room for it), the upgrade remembered
// for its creation
bool UpgradeForAllocation(ID3D12Device* device, D3D12_RESOURCE_DESC& desc);
// GetDesc: the game's desc of a texture upgraded in place, when the rules spoof it
void SpoofDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc);
void SpoofDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC1& desc);
// What the game asked for and what the texture became, none if it isn't upgraded
std::optional<Upgrade::UpgradeInfo> FindUpgrade(ID3D12Resource* resource);
ID3D12Resource* FindMirror(ID3D12Resource* resource);
ID3D12Resource* FindOriginal(ID3D12Resource* mirror);
ID3D12Resource* GetEffectiveResource(ID3D12Resource* resource);
std::optional<MirrorSummary> FindMirrorSummary(ID3D12Resource* resource);
// Presenting thread: resolution invalidation, materializes queued chain mirrors with committed states, recycles pool
void FlushFrame(ID3D12Device* device);
void InvalidateScaledMirrors(ID3D12Device* device);
// The game released the device: Luma's descriptor heaps and pools on it go
void ReleaseDeviceObjects(ID3D12Device* device);
Stats GetStats();

// Heaps, views and descriptors (D3D12MirrorsDescriptors.cpp)
// A heap the game created: shader-visible ones are where copies are swapped
void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc);
// A view the game creates: its format in an in-place resource's (UNKNOWN: unchanged). Render target and depth handles remember an in-place
// resource's format and scale for their binds.
DXGI_FORMAT PrepareView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, DXGI_FORMAT game_format,
   bool has_desc);
// The game wrote a view at `destination`: one of a mirrored texture gets the mirror's, any other forgets what the handle had. `swapping`
// (Mirrors::IsSwapping) is for a view written straight into a shader-visible heap.
void TrackView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping);
// The same for samplers, `swapping` being Samplers::IsSwapping
void TrackSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping);
// After the game's copy of `count` descriptors (one run of CopyDescriptors): the destination handles hold what the sources did, and a
// shader-visible destination gets the mirrors' views (`swap.views`, Mirrors::IsSwapping) and samplers (`swap.samplers`)
void TrackCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   DescriptorSwapOptions swap);
D3D12_CPU_DESCRIPTOR_HANDLE FindMirrorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle);
D3D12_CPU_DESCRIPTOR_HANDLE FindOriginalHandle(D3D12_CPU_DESCRIPTOR_HANDLE mirror_handle);
ID3D12Resource* GetDescriptorResource(D3D12_CPU_DESCRIPTOR_HANDLE handle);
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GetResourceDescriptors(ID3D12Resource* resource);

// Copies, resolves and clears (D3D12MirrorsCopies.cpp)
// Copies and resolves with an upgraded texture. True: Luma ran it (converted or scaled), or skipped one it couldn't; the game's doesn't run.
// False: the game's runs, with `region` rescaled for a resized texture and on the mirrors while `swapping`.
bool Copy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, CopyRegion& region, bool swapping);
// An upload or a readback: a footprint of the game's format and size
bool CopyFootprint(ID3D12GraphicsCommandList* list, D3D12_TEXTURE_COPY_LOCATION& destination, D3D12_TEXTURE_COPY_LOCATION& source, CopyRegion& region, bool swapping);
bool Resolve(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, UINT destination_subresource, ID3D12Resource*& source, UINT source_subresource,
   DXGI_FORMAT& format, bool swapping);
// Clears: the rects of a resized in-place target in its size, the mirror's view while `swapping`
void PrepareClear(D3D12_CPU_DESCRIPTOR_HANDLE& view, UINT rect_count, const D3D12_RECT*& rects, bool swapping);
void PrepareClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource,
   UINT rect_count, bool swapping);

// Barriers, scaling and draw hooks (D3D12MirrorsScaling.cpp)
// The game's barriers with the mirrors' appended, and tracks per-list barrier transitions for submission-order state tracking
std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(ID3D12GraphicsCommandList* list, std::span<const D3D12_RESOURCE_BARRIER> barriers);
// Commits each executed command list's final barrier states in submission order
void CommitExecutedBarriers(UINT count, ID3D12CommandList* const* lists);
// The game's targets: mirrors swapped in while `swapping` (the handles point into thread-local storage valid until the next call), the
// formats a pipeline's variant needs returned, and the viewports and scissors of a resized in-place target scaled while it's bound
Shaders::Database::D3D12::TargetFormats BindRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil, bool swapping);
void BindViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports);
void BindScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects);
void PrepareDrawOrDispatch(ID3D12GraphicsCommandList* list, bool is_dispatch);
void FinishDrawOrDispatch(ID3D12GraphicsCommandList* list, bool is_dispatch);
void ForgetListState(ID3D12GraphicsCommandList* list);
std::optional<ListScalingSnapshot> GetListScalingSnapshot(ID3D12GraphicsCommandList* list);
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
