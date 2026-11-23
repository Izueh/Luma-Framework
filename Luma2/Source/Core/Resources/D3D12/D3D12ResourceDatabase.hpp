#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <d3d12.h>

#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceDatabase.hpp"

// Resources::Database on D3D12: the fulfil step for Luma's cbuffers and textures. Cbuffer contents go into an upload ring per device
// (memory a command list wrote into lives until the list has executed) and are bound as root CBVs at Luma's parameters of the
// extended root signature (Shaders::RootSignatures::D3D12), which the game never sets: nothing is restored after a draw. Textures
// are created and uploaded at the device's creation; a variant reads them through Luma's texture table, whose descriptors are in
// the bound heap's tail (Resources::HeapTail::D3D12), one range per table layout. Luma's shaders as objects aren't bound on D3D12
// yet.
namespace Luma {
namespace Resources {
namespace Database {
namespace D3D12 {
// The entry at each register of Luma's texture table (Bindings::D3D12_FIRST_TEXTURE_REGISTER on), UINT32_MAX for none
using TableLayout = std::array<uint32_t, Bindings::D3D12_TEXTURE_REGISTERS>;
inline constexpr uint32_t NO_TABLE = UINT32_MAX;
// Any thread: the id of a layout, the same for the same layout; NO_TABLE when there's no room for another in the heaps' tails
uint32_t RegisterTable(const TableLayout& layout);

// The device's upload ring and Luma's textures (uploaded and waited for here)
void CreateDeviceObjects(ID3D12Device* device);
// The game released the device
void ReleaseDeviceObjects(ID3D12Device* device);

// The list's root signature changed: its root arguments are gone, Luma's included
void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
// The list was reset or cleared: nothing is uploaded or bound for it
void ForgetList(ID3D12GraphicsCommandList* list);
// The list's CBV/SRV/UAV heap, which Luma's table points into
void SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps);
// Before a draw, dispatch or indirect execution: the cbuffer `entries` are uploaded where their contents changed for this list
// (LumaData with `custom`) and bound at Luma's parameters of the list's graphics or compute signature, and `table` (a layout of
// RegisterTable) at Luma's table parameter. False when the table can't be bound (a heap without a tail, a signature without the
// table): the caller runs the game's shaders. Nothing while Luma is off.
bool BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom = {}, uint32_t table = NO_TABLE);
// Before a bundle runs: Luma's cbuffers bound on the list's graphics signature, which the bundle inherits
void BindForBundle(ID3D12GraphicsCommandList* list);

struct Stats
{
   uint64_t ring_bytes = 0; // Allocated so far, of chunks of the ring
   uint32_t chunks = 0;
   uint32_t textures = 0; // Created, on every device
   uint32_t tables = 0; // Layouts registered
};

Stats GetStats();
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
