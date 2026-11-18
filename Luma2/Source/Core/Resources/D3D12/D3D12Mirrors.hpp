#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <d3d12.h>

// Mirrors on D3D12 (Guidelines.md sections 4 and 6). A texture the upgrade rules match gets a committed mirror at its creation, in its
// initial state, held in its private data. Every barrier on the game's texture is given to the mirror too, so the two are always in
// the same state and either can be used.
//
// Views: when the game writes a view of a mirrored texture (anywhere), Luma writes the same view of the mirror into a CPU heap of its
// own and remembers it for the game's handle; a sampler the rules change gets its mirror the same way. Copies of descriptors carry
// that along, and a copy into a shader-visible heap is followed by the mirror's view over each mirrored descriptor while swapping: the
// game's CPU heaps keep the game's views, and a toggle takes effect at the next copy. A view written straight into a shader-visible
// heap is swapped there at once (it doesn't follow a toggle). Render targets are swapped at OMSetRenderTargets and clears, and the
// pipeline bound gets a variant with the mirrors' formats at the draw (Shaders::Variants::D3D12). Copies between mirrors run on the
// mirrors.
namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
// The game created `resource`, in `state` (legacy barriers), with `clear` as its optimized clear value (or none)
void CreateMirror(ID3D12Device* device, ID3D12Resource* resource, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear);
ID3D12Resource* GetMirror(ID3D12Resource* resource);
ID3D12Resource* GetOriginal(ID3D12Resource* mirror);
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
void OnCreateView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping);
// The same for samplers, `swapping` being Samplers::IsSwapping
void OnCreateSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping);
// After the game's copy of `count` descriptors (one run of CopyDescriptors): the destination handles hold what the sources did, and a
// shader-visible destination gets the mirrors' views (`swap_views`, Mirrors::IsSwapping) and samplers (`swap_samplers`)
void OnCopyDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   bool swap_views, bool swap_samplers);

// The game's barriers with the mirrors' appended: `barriers` itself when none is mirrored, else storage of the calling thread valid
// until its next call
std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(std::span<const D3D12_RESOURCE_BARRIER> barriers);

// The rest is called while swapping (Mirrors::IsSwapping).
// OMSetRenderTargets: mirrored targets replaced by their mirrors' views (the handles then point to storage of the
// calling thread, valid until its next call, and `single_range` is false). `formats` gets each swapped target's format (UNKNOWN for
// the game's own), for the pipeline's format variant.
void SwapRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range, const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil,
   std::array<DXGI_FORMAT, 8>& formats);
// A view handle of a mirrored texture (a clear). False when it isn't one.
bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle);
// CopyResource, CopyTextureRegion's subresources, ResolveSubresource: both sides mirrored are copied between the mirrors. One side
// mirrored and the other not (a format change, which needs Luma's copy pass) keeps the game's copy and is counted.
void SwapCopy(ID3D12Resource*& destination, ID3D12Resource*& source);
// ClearUnorderedAccessView*: a mirrored texture's UAV clear needs the mirror's view in a shader-visible heap (not built yet): counted
void OnClearUnorderedAccess(ID3D12Resource* resource);

struct Stats
{
   uint32_t descriptors = 0; // Game handles that hold a mirrored view or sampler
   uint32_t samplers = 0; // Sampler mirrors written
   uint32_t swapped_copies = 0; // Descriptors swapped at a copy into a shader-visible heap, since the start
   uint32_t direct_writes = 0; // Views of mirrors written straight into a shader-visible heap
   uint32_t unhandled_copies = 0; // Copies between a mirror and a texture without one
   uint32_t unhandled_clears = 0; // UAV clears of mirrored textures
   uint32_t layout_resources = 0; // Rule-matching textures created with an enhanced barrier layout: not mirrored
   uint32_t aliasing_barriers = 0; // On mirrored textures
};

Stats GetStats();
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
