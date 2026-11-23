#include "Resources/D3D12/D3D12Descriptors.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "Hooks/ObjectLifetime.hpp"

namespace Luma {
namespace Resources {
namespace Descriptors {
namespace D3D12 {
namespace {
// A heap's descriptors, allocated a chunk at a time as they're written: a large shader-visible heap is mostly never read
constexpr uint32_t CHUNK = 1024;

struct Heap
{
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint64_t cpu_start = 0;
   uint64_t gpu_start = 0; // 0 for a heap that isn't shader visible
   uint32_t count = 0;
   uint32_t increment = 0;
   // Never held with another lock but the registry's (shared)
   std::mutex mutex;
   std::vector<std::unique_ptr<Descriptor[]>> chunks;

   uint64_t GetEnd(uint64_t start) const { return start + uint64_t(count) * increment; }
};

// Lock order: heaps_mutex, then a heap's mutex
std::shared_mutex heaps_mutex;
std::vector<std::shared_ptr<Heap>> by_cpu; // Sorted by cpu_start
std::vector<std::shared_ptr<Heap>> by_gpu; // Sorted by gpu_start, the shader-visible ones
std::atomic<uint64_t> written_count = 0;

// In the heap's private data: its table goes with it
struct HeapState
{
   std::shared_ptr<Heap> heap;

   ~HeapState()
   {
      std::scoped_lock lock(heaps_mutex);
      std::erase(by_cpu, heap);
      std::erase(by_gpu, heap);
   }
};

// Never held with another lock
std::shared_mutex resources_mutex;
std::unordered_map<const void*, ResourceInfo> resources;
std::map<uint64_t, const void*> buffers; // By GPU address
std::atomic<uint64_t> next_id = 1;

// In the resource's private data: it's forgotten with it
struct ResourceState
{
   const void* resource = nullptr;
   uint64_t id = 0;

   ~ResourceState()
   {
      std::scoped_lock lock(resources_mutex);
      const auto it = resources.find(resource);
      if (it == resources.end() || it->second.id != id)
         return;
      if (it->second.address)
         buffers.erase(it->second.address);
      resources.erase(it);
   }
};

// The heap of `handle` among `heaps` sorted by `start`, under heaps_mutex
Heap* FindHeap(const std::vector<std::shared_ptr<Heap>>& heaps, uint64_t handle, uint64_t Heap::* start)
{
   const auto it = std::ranges::upper_bound(heaps, handle, {}, [&](const std::shared_ptr<Heap>& heap) { return (*heap).*start; });
   if (it == heaps.begin())
      return nullptr;
   Heap* heap = std::prev(it)->get();
   return handle < heap->GetEnd((*heap).*start) ? heap : nullptr;
}

void Insert(std::vector<std::shared_ptr<Heap>>& heaps, const std::shared_ptr<Heap>& heap, uint64_t Heap::* start)
{
   const auto it = std::ranges::upper_bound(heaps, (*heap).*start, {}, [&](const std::shared_ptr<Heap>& other) { return (*other).*start; });
   heaps.insert(it, heap);
}

// Under the heap's mutex
Descriptor* GetSlot(Heap& heap, uint32_t index, bool allocate)
{
   const size_t chunk = index / CHUNK;
   if (chunk >= heap.chunks.size() || !heap.chunks[chunk])
   {
      if (!allocate)
         return nullptr;
      if (chunk >= heap.chunks.size())
         heap.chunks.resize(chunk + 1);
      heap.chunks[chunk] = std::make_unique<Descriptor[]>(CHUNK);
   }
   return &heap.chunks[chunk][index % CHUNK];
}

uint64_t GetResourceId(const ID3D12Resource* resource)
{
   if (!resource)
      return 0;
   std::shared_lock lock(resources_mutex);
   const auto it = resources.find(resource);
   return it != resources.end() ? it->second.id : 0;
}
} // namespace

void TrackResource(ID3D12Resource* resource)
{
   if (!resource || Hooks::Lifetime::GetState<ResourceState>(resource))
      return;
   ResourceInfo info = {.id = next_id++, .resource = resource, .desc = resource->GetDesc()};
   D3D12_HEAP_PROPERTIES properties = {};
   if (SUCCEEDED(resource->GetHeapProperties(&properties, nullptr)))
      info.heap_type = properties.Type;
   if (info.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
   {
      info.address = resource->GetGPUVirtualAddress();
      // Mapping an upload heap buffer for good is allowed, and the game's own maps get the same address
      void* mapped = nullptr;
      const D3D12_RANGE nothing_read = {0, 0};
      if (info.heap_type == D3D12_HEAP_TYPE_UPLOAD && SUCCEEDED(resource->Map(0, &nothing_read, &mapped)))
         info.mapped = static_cast<const std::byte*>(mapped);
   }
   if (!Hooks::Lifetime::SetState<ResourceState>(resource, resource, info.id))
      return;
   std::scoped_lock lock(resources_mutex);
   if (info.address)
      buffers[info.address] = resource;
   resources[resource] = info;
}

std::optional<ResourceInfo> FindResource(const ID3D12Resource* resource)
{
   std::shared_lock lock(resources_mutex);
   const auto it = resources.find(resource);
   return it != resources.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<ResourceInfo> FindBuffer(D3D12_GPU_VIRTUAL_ADDRESS address)
{
   std::shared_lock lock(resources_mutex);
   auto it = buffers.upper_bound(address);
   if (it == buffers.begin())
      return std::nullopt;
   --it;
   const ResourceInfo& info = resources.at(it->second);
   if (address >= info.address + info.desc.Width)
      return std::nullopt;
   return info;
}

void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   auto table = std::make_shared<Heap>();
   table->type = desc.Type;
   table->count = desc.NumDescriptors;
   table->increment = device->GetDescriptorHandleIncrementSize(desc.Type);
   table->cpu_start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
   if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
      table->gpu_start = heap->GetGPUDescriptorHandleForHeapStart().ptr;
   if (!Hooks::Lifetime::SetState<HeapState>(heap, table))
      return;
   std::scoped_lock lock(heaps_mutex);
   Insert(by_cpu, table, &Heap::cpu_start);
   if (table->gpu_start)
      Insert(by_gpu, table, &Heap::gpu_start);
}

void Write(D3D12_CPU_DESCRIPTOR_HANDLE destination, Descriptor descriptor)
{
   descriptor.resource_id = GetResourceId(descriptor.resource);
   std::shared_lock lock(heaps_mutex);
   Heap* heap = FindHeap(by_cpu, destination.ptr, &Heap::cpu_start);
   if (!heap)
      return;
   std::scoped_lock heap_lock(heap->mutex);
   *GetSlot(*heap, uint32_t((destination.ptr - heap->cpu_start) / heap->increment), true) = descriptor;
   written_count.fetch_add(1, std::memory_order_relaxed);
}

void Copy(D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, uint32_t count)
{
   std::shared_lock lock(heaps_mutex);
   Heap* to = FindHeap(by_cpu, destination.ptr, &Heap::cpu_start);
   Heap* from = FindHeap(by_cpu, source.ptr, &Heap::cpu_start);
   if (!to)
      return;
   const uint32_t to_index = uint32_t((destination.ptr - to->cpu_start) / to->increment);
   count = std::min(count, to->count - to_index);
   // Read first: the two heaps' locks are never held together
   std::vector<Descriptor> copied(count);
   if (from)
   {
      const uint32_t from_index = uint32_t((source.ptr - from->cpu_start) / from->increment);
      std::scoped_lock from_lock(from->mutex);
      for (uint32_t i = 0; i < count && from_index + i < from->count; i++)
      {
         if (const Descriptor* slot = GetSlot(*from, from_index + i, false))
            copied[i] = *slot;
      }
   }
   std::scoped_lock to_lock(to->mutex);
   for (uint32_t i = 0; i < count; i++)
      *GetSlot(*to, to_index + i, true) = copied[i];
   written_count.fetch_add(count, std::memory_order_relaxed);
}

std::optional<Descriptor> Read(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   std::shared_lock lock(heaps_mutex);
   Heap* heap = FindHeap(by_cpu, handle.ptr, &Heap::cpu_start);
   if (!heap)
      return std::nullopt;
   std::scoped_lock heap_lock(heap->mutex);
   const Descriptor* slot = GetSlot(*heap, uint32_t((handle.ptr - heap->cpu_start) / heap->increment), false);
   return slot ? *slot : Descriptor();
}

std::optional<Descriptor> Read(D3D12_GPU_DESCRIPTOR_HANDLE table, uint32_t index)
{
   std::shared_lock lock(heaps_mutex);
   Heap* heap = FindHeap(by_gpu, table.ptr, &Heap::gpu_start);
   if (!heap)
      return std::nullopt;
   const uint64_t first = (table.ptr - heap->gpu_start) / heap->increment;
   if (first + index >= heap->count)
      return std::nullopt;
   std::scoped_lock heap_lock(heap->mutex);
   const Descriptor* slot = GetSlot(*heap, uint32_t(first + index), false);
   return slot ? *slot : Descriptor();
}

Stats GetStats()
{
   Stats stats;
   {
      std::shared_lock lock(heaps_mutex);
      stats.heaps = uint32_t(by_cpu.size());
      for (const std::shared_ptr<Heap>& heap : by_cpu)
         stats.descriptors += heap->count;
   }
   std::shared_lock lock(resources_mutex);
   stats.resources = uint32_t(resources.size());
   stats.buffers = uint32_t(buffers.size());
   return stats;
}
} // namespace D3D12
} // namespace Descriptors
} // namespace Resources
} // namespace Luma
