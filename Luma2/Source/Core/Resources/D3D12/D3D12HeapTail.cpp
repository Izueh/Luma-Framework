#include "Resources/D3D12/D3D12HeapTail.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Resources {
namespace HeapTail {
namespace D3D12 {
namespace {
// D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1 and _TIER_2. Tier 3 hardware may take more, which nothing asks for yet.
constexpr uint64_t MAX_DESCRIPTORS = 1000000;

std::atomic<uint32_t> heaps = 0;
std::atomic<uint32_t> refused = 0;

// In the heap's private data
struct Tail
{
   uint32_t game_size = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE cpu = {}; // The tail's first descriptor
   D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
   uint32_t increment = 0;
   std::mutex mutex; // Never held with another
   std::vector<uint32_t> written; // Keys written so far

   Tail() { heaps++; }

   ~Tail() { heaps--; }
};
} // namespace

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> Extend(const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
      return std::nullopt;
   if (uint64_t(desc.NumDescriptors) + TAIL_SIZE > MAX_DESCRIPTORS)
   {
      if (refused++ == 0)
         Log::Warning("D3D12: a shader-visible heap of {} descriptors has no room for Luma's {}: Luma's textures can't be bound with it", desc.NumDescriptors, TAIL_SIZE);
      return std::nullopt;
   }
   D3D12_DESCRIPTOR_HEAP_DESC extended = desc;
   extended.NumDescriptors += TAIL_SIZE;
   return extended;
}

void Track(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc)
{
   Tail* tail = Hooks::Lifetime::SetState<Tail>(heap);
   if (!tail)
      return;
   tail->game_size = game_desc.NumDescriptors;
   tail->increment = device->GetDescriptorHandleIncrementSize(game_desc.Type);
   tail->cpu = heap->GetCPUDescriptorHandleForHeapStart();
   tail->cpu.ptr += SIZE_T(tail->increment) * game_desc.NumDescriptors;
   tail->gpu = heap->GetGPUDescriptorHandleForHeapStart();
   tail->gpu.ptr += UINT64(tail->increment) * game_desc.NumDescriptors;
}

std::optional<uint32_t> GetGameSize(ID3D12DescriptorHeap* heap)
{
   const Tail* tail = heap ? Hooks::Lifetime::GetState<Tail>(heap) : nullptr;
   if (!tail)
      return std::nullopt;
   return tail->game_size;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetRange(ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count, uint32_t key,
   const std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment)>& write)
{
   Tail* tail = heap ? Hooks::Lifetime::GetState<Tail>(heap) : nullptr;
   if (!tail || uint64_t(offset) + count > TAIL_SIZE)
      return {};
   {
      std::scoped_lock lock(tail->mutex);
      if (std::find(tail->written.begin(), tail->written.end(), key) == tail->written.end())
      {
         write({tail->cpu.ptr + SIZE_T(tail->increment) * offset}, tail->increment);
         tail->written.push_back(key);
      }
   }
   return {tail->gpu.ptr + UINT64(tail->increment) * offset};
}

Stats GetStats()
{
   return {heaps.load(), refused.load()};
}
} // namespace D3D12
} // namespace HeapTail
} // namespace Resources
} // namespace Luma
