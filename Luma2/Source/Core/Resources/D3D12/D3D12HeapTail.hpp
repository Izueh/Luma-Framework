#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <d3d12.h>

namespace Luma {
namespace Resources {
namespace HeapTail {
namespace D3D12 {
inline constexpr uint32_t FIXED_SIZE = 256;
inline constexpr uint32_t RING_BLOCK_SIZE = 64;
inline constexpr uint32_t RING_BLOCKS = 16;
inline constexpr uint32_t RING_SIZE = RING_BLOCK_SIZE * RING_BLOCKS;
inline constexpr uint32_t TAIL_SIZE = FIXED_SIZE + RING_SIZE;
inline constexpr uint64_t TIER_2_LIMIT = 1000000;

std::optional<D3D12_DESCRIPTOR_HEAP_DESC> Extend(const D3D12_DESCRIPTOR_HEAP_DESC& desc);
// Returns next fallback desc to try, or nullopt on success or final refusal
std::optional<D3D12_DESCRIPTOR_HEAP_DESC> RecordExtension(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc,
   const D3D12_DESCRIPTOR_HEAP_DESC& extended, HRESULT result);
void Track(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc, uint32_t tail_size);
std::optional<uint32_t> GetGameSize(ID3D12DescriptorHeap* heap);

// `write` runs once per `key` in `heap`; returns null handle if no tail or out of fixed range
D3D12_GPU_DESCRIPTOR_HANDLE GetRange(ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count, uint32_t key,
   const std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment)>& write);

struct RingRange
{
   D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
   D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
   uint32_t increment = 0;
};

// Valid until the list's executions complete
std::optional<RingRange> AllocateRing(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* heap, uint32_t count);
void ForgetList(ID3D12GraphicsCommandList* list);

struct Stats
{
   uint32_t heaps = 0;
   uint32_t fixed_only = 0;
   uint32_t refused = 0;
   uint32_t over_tier_2 = 0;
   uint32_t over_tier_2_refused = 0;
   uint32_t ring_blocks = 0;
   uint32_t ring_full = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace HeapTail
} // namespace Resources
} // namespace Luma
