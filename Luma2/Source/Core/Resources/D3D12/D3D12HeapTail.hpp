#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <d3d12.h>

// Luma's descriptors in the game's shader-visible heaps: every shader-visible CBV/SRV/UAV heap the game
// creates gets TAIL_SIZE more descriptors at its end, which the game never sees (GetDesc answers its own size) and so never writes. Luma
// writes its views there, valid while the game's heap is bound, so its tables need no heap of their own (a list binds one heap of a
// type). A heap whose size with the tail would pass the device's limit gets none: Luma's tables can't be bound while it is.
namespace Luma {
namespace Resources {
namespace HeapTail {
namespace D3D12 {
inline constexpr uint32_t TAIL_SIZE = 256;

// Before the game's CreateDescriptorHeap: the desc with Luma's tail, none when the heap gets no tail
std::optional<D3D12_DESCRIPTOR_HEAP_DESC> Extend(const D3D12_DESCRIPTOR_HEAP_DESC& desc);
// The heap was created from Extend's desc: `game_desc` is what the game asked for
void Track(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& game_desc);
// The game's size of a heap with a tail, for its GetDesc
std::optional<uint32_t> GetGameSize(ID3D12DescriptorHeap* heap);

// The GPU handle of `count` descriptors at `offset` in the heap's tail, which `write` fills (CPU handle, increment) the first time
// `key` is asked for in this heap (a key is one fixed content, say a table layout). Null (ptr 0) for a heap without a tail.
D3D12_GPU_DESCRIPTOR_HANDLE GetRange(ID3D12DescriptorHeap* heap, uint32_t offset, uint32_t count, uint32_t key,
   const std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment)>& write);

struct Stats
{
   uint32_t heaps = 0; // With a tail, alive
   uint32_t refused = 0; // Too big for one
};

Stats GetStats();
} // namespace D3D12
} // namespace HeapTail
} // namespace Resources
} // namespace Luma
