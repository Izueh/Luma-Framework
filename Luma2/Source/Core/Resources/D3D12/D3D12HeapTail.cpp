#include "Resources/D3D12/D3D12HeapTail.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "System/MonitorDisplay.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace HeapTail {
namespace D3D12 {
namespace {
std::atomic<uint32_t> heaps = 0;
std::atomic<uint32_t> fixed_only = 0;
std::atomic<uint32_t> refused = 0;
std::atomic<uint32_t> over_tier_2 = 0;
std::atomic<uint32_t> over_tier_2_refused = 0;
std::atomic<uint32_t> ring_blocks = 0;
std::atomic<uint32_t> ring_full = 0;
// [past TIER_2_LIMIT][answer: taken, refused with the ring, refused without]
std::array<std::array<std::atomic<bool>, 3>, 2> logged = {};

// Shared with the blocks lists hold, which can outlive the heap
struct Ring
{
   std::mutex mutex; // Never held with another
   std::bitset<RING_BLOCKS> held;
};

// Its last release, by the list or a queue it executed on, gives the block back
struct RingBlock final : IUnknown
{
   std::atomic<ULONG> references = 1;
   std::shared_ptr<Ring> ring;
   uint32_t index = 0;

   RingBlock(std::shared_ptr<Ring> ring_, uint32_t index_) : ring(std::move(ring_)), index(index_) { ring_blocks++; }

   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override
   {
      if (!result)
         return E_POINTER;
      if (riid == __uuidof(IUnknown))
      {
         AddRef();
         *result = static_cast<IUnknown*>(this);
         return S_OK;
      }
      *result = nullptr;
      return E_NOINTERFACE;
   }

   ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

   ULONG STDMETHODCALLTYPE Release() override
   {
      const ULONG remaining = --references;
      if (remaining == 0)
      {
         {
            std::scoped_lock lock(ring->mutex);
            ring->held.reset(index);
         }
         ring_blocks--;
         delete this;
      }
      return remaining;
   }
};

// In the heap's private data
struct Tail
{
   uint32_t game_size = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE cpu = {}; // The tail's first descriptor
   D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
   uint32_t increment = 0;
   std::mutex mutex; // Never held with another
   std::vector<uint32_t> written; // Keys written so far
   std::shared_ptr<Ring> ring; // None when the device took the fixed section only
   bool full_logged = false; // Under the ring's mutex

   Tail() { heaps++; }

   ~Tail() { heaps--; }
};

// Recorded by one thread at a time
struct ListRing
{
   ID3D12DescriptorHeap* heap = nullptr;
   RingBlock* block = nullptr; // Alive while the list holds it, until ForgetList
   uint32_t used = 0;
};

RingBlock* TakeBlock(ID3D12GraphicsCommandList* list, Tail& tail)
{
   uint32_t index = RING_BLOCKS;
   {
      std::scoped_lock lock(tail.ring->mutex);
      for (uint32_t i = 0; i < RING_BLOCKS; i++)
      {
         if (!tail.ring->held.test(i))
         {
            index = i;
            tail.ring->held.set(i);
            break;
         }
      }
      if (index == RING_BLOCKS)
      {
         ring_full++;
         if (!std::exchange(tail.full_logged, true))
            Log::Warning("D3D12: every block of a heap's ring of Luma descriptors ({} of {}) is held by lists not executed yet: views that change per draw fall back",
               RING_BLOCKS, RING_BLOCK_SIZE);
         return nullptr;
      }
   }
   ComPtr<IUnknown> block;
   block.Attach(new RingBlock(tail.ring, index));
   auto* taken = static_cast<RingBlock*>(block.Get());
   // Only the list's reference is left once `holder` goes
   Hooks::Lifetime::D3D12::Object holder;
   Hooks::Lifetime::D3D12::Replace(holder, std::move(block));
   return Hooks::Lifetime::D3D12::Reference(list, holder) ? taken : nullptr;
}
} // namespace

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> Extend(const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
      return std::nullopt;
   if (uint64_t(desc.NumDescriptors) + TAIL_SIZE > UINT32_MAX)
      return std::nullopt;
   D3D12_DESCRIPTOR_HEAP_DESC extended = desc;
   extended.NumDescriptors += TAIL_SIZE;
   return extended;
}

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> RecordExtension(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc,
   const D3D12_DESCRIPTOR_HEAP_DESC& extended, HRESULT result)
{
   const bool large = extended.NumDescriptors > TIER_2_LIMIT;
   const bool with_ring = extended.NumDescriptors - game_desc.NumDescriptors >= TAIL_SIZE;
   const bool was_refused = FAILED(result);
   if (large)
      (was_refused ? over_tier_2_refused : over_tier_2)++;
   std::optional<D3D12_DESCRIPTOR_HEAP_DESC> next;
   if (was_refused && with_ring)
   {
      next = game_desc;
      next->NumDescriptors += FIXED_SIZE;
   }
   else if (was_refused)
      refused++;
   else if (!with_ring)
      fixed_only++;
   // Only the answers past the tier 2 limit say something about the hardware; a smaller heap refused is unexpected
   const size_t answer = was_refused ? (with_ring ? 1 : 2) : 0;
   if ((!large && !was_refused) || logged[large][answer].exchange(true))
      return next;
   D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
   device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
   const std::string adapter = System::Display::GetAdapterName(device->GetAdapterLuid());
   const uint32_t tail = extended.NumDescriptors - game_desc.NumDescriptors;
   if (was_refused)
      Log::Warning("D3D12: {} refused a shader-visible heap of {} descriptors (the game's {} and Luma's {}), resource binding tier {} (0x{:08X}): {}", adapter,
         extended.NumDescriptors, game_desc.NumDescriptors, tail, int(options.ResourceBindingTier), uint32_t(result),
         with_ring ? "Luma's views that change per draw can't be bound with it" : "created at the game's size, Luma's tables can't be bound with it");
   else
      Log::Info("D3D12: {} took a shader-visible heap of {} descriptors (past {}, with Luma's {}), resource binding tier {}", adapter, extended.NumDescriptors,
         TIER_2_LIMIT, tail, int(options.ResourceBindingTier));
   return next;
}

void Track(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc, uint32_t tail_size)
{
   Tail* tail = Hooks::Lifetime::SetState<Tail>(heap);
   if (!tail)
      return;
   if (tail_size >= TAIL_SIZE)
      tail->ring = std::make_shared<Ring>();
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
   if (!tail || uint64_t(offset) + count > FIXED_SIZE)
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

std::optional<RingRange> AllocateRing(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* heap, uint32_t count)
{
   Tail* tail = heap ? Hooks::Lifetime::GetState<Tail>(heap) : nullptr;
   ListRing* state = Hooks::Lifetime::GetOrCreateStateCached<ListRing>(list);
   if (!tail || !tail->ring || !state || count == 0 || count > RING_BLOCK_SIZE)
      return std::nullopt;
   if (state->heap != heap || !state->block || state->used + count > RING_BLOCK_SIZE)
   {
      RingBlock* block = TakeBlock(list, *tail);
      if (!block)
         return std::nullopt;
      *state = {heap, block, 0};
   }
   const uint32_t offset = FIXED_SIZE + state->block->index * RING_BLOCK_SIZE + state->used;
   state->used += count;
   return RingRange{{tail->cpu.ptr + SIZE_T(tail->increment) * offset}, {tail->gpu.ptr + UINT64(tail->increment) * offset}, tail->increment};
}

void ForgetList(ID3D12GraphicsCommandList* list)
{
   if (ListRing* state = Hooks::Lifetime::GetState<ListRing>(list))
      *state = {};
}

Stats GetStats()
{
   return {heaps.load(), fixed_only.load(), refused.load(), over_tier_2.load(), over_tier_2_refused.load(), ring_blocks.load(), ring_full.load()};
}
} // namespace D3D12
} // namespace HeapTail
} // namespace Resources
} // namespace Luma
