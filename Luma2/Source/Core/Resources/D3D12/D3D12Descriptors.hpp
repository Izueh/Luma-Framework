#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <d3d12.h>

// What D3D12 descriptors point to. Descriptors aren't objects (Guidelines.md section 5): a side table per descriptor heap keeps, per
// descriptor, the resource (not held) and the view desc, written by Create*View and CopyDescriptors*, read by CPU or GPU handle.
// The game's resources are known from their creation: their desc, a buffer's GPU address range (root CBVs and SRVs name buffers
// by address), and the CPU copy of an upload heap buffer. Trace reads them in Development builds; the mirrors add their
// need. Any thread.
namespace Luma {
namespace Resources {
namespace Descriptors {
namespace D3D12 {
enum class Kind : uint8_t
{
   None, // Never written, or copied from a descriptor Luma didn't see
   ConstantBuffer,
   ShaderResource,
   UnorderedAccess,
   RenderTarget,
   DepthStencil,
   Sampler,
};

struct Descriptor
{
   Kind kind = Kind::None;
   bool has_desc = false; // The game passed a desc; without one the view is the resource's default
   ID3D12Resource* resource = nullptr; // Not held; null for a null descriptor, a CBV and a sampler
   uint64_t resource_id = 0; // TrackResource's, to tell a resource from a later one at the same address; 0 when unknown
   union
   {
      D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer;
      D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource;
      D3D12_UNORDERED_ACCESS_VIEW_DESC unordered_access;
      D3D12_RENDER_TARGET_VIEW_DESC render_target;
      D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil;
      D3D12_SAMPLER_DESC sampler;
   } desc = {};
};

struct ResourceInfo
{
   uint64_t id = 0;
   ID3D12Resource* resource = nullptr; // Alive when this was read
   D3D12_RESOURCE_DESC desc = {};
   D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT; // Unknown for reserved resources: default
   D3D12_GPU_VIRTUAL_ADDRESS address = 0; // Buffers
   // An upload heap buffer's memory, mapped by Luma for its lifetime: what the game wrote for the GPU
   const std::byte* mapped = nullptr;
};

// The game created a resource (or Luma found a swapchain's buffer): known until it's destroyed
void TrackResource(ID3D12Resource* resource);
std::optional<ResourceInfo> FindResource(const ID3D12Resource* resource);
// The buffer whose range has `address`
std::optional<ResourceInfo> FindBuffer(D3D12_GPU_VIRTUAL_ADDRESS address);

// A heap the game created: its descriptors are tracked until it's destroyed
void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc);
// Create*View into `destination`; the descriptor's resource_id is filled here
void Write(D3D12_CPU_DESCRIPTOR_HANDLE destination, Descriptor descriptor);
// CopyDescriptors' ranges, one pair of starts at a time
void Copy(D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, uint32_t count);
// None outside the tracked heaps
std::optional<Descriptor> Read(D3D12_CPU_DESCRIPTOR_HANDLE handle);
// The `index`-th descriptor from `table` (a root table's start) in its shader-visible heap
std::optional<Descriptor> Read(D3D12_GPU_DESCRIPTOR_HANDLE table, uint32_t index);

struct Stats
{
   uint32_t heaps = 0;
   uint64_t descriptors = 0; // Of the tracked heaps
   uint32_t resources = 0;
   uint32_t buffers = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace Descriptors
} // namespace Resources
} // namespace Luma
