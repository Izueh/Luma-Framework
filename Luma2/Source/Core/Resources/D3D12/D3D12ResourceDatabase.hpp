#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>

#include "Resources/ResourceDatabase.hpp"

// Resources::Database on D3D12: the fulfil step for Luma's cbuffers. Their contents go into an upload ring per device (memory a
// command list wrote into lives until the list has executed) and are bound as root CBVs at Luma's parameters of the extended
// root signature (Shaders::RootSignatures::D3D12), which the game never sets: nothing is restored after a draw. Luma's shaders as
// objects come with the D3D12 passes (Step 5).
namespace Luma {
namespace Resources {
namespace Database {
namespace D3D12 {
// The device's upload ring
void CreateDeviceObjects(ID3D12Device* device);
// The game released the device
void ReleaseDeviceObjects(ID3D12Device* device);

// The list's root signature changed: its root arguments are gone, Luma's included
void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
// The list was reset or cleared: nothing is uploaded or bound for it
void ForgetList(ID3D12GraphicsCommandList* list);
// Before a draw, dispatch or indirect execution: the `entries` are uploaded where their contents changed for this list (LumaData
// with `custom`) and bound at Luma's parameters of the list's graphics or compute signature. Nothing while Luma is off.
void BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom = {});
// Before a bundle runs: Luma's cbuffers bound on the list's graphics signature, which the bundle inherits
void BindForBundle(ID3D12GraphicsCommandList* list);

struct Stats
{
   uint64_t ring_bytes = 0; // Allocated so far, of chunks of the ring
   uint32_t chunks = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
