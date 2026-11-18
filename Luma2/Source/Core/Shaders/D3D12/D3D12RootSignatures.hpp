#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Shaders/D3D12/D3D12Database.hpp"

struct ID3D12Device;

// Every root signature the game creates gets Luma's parameters appended, and the game gets that signature: its pipelines and Luma's
// variants then share it, so a variant is swapped in at SetPipelineState with nothing to do at the draw. The game's parameter
// indices don't move, and it can't read a signature back.
namespace Luma {
namespace Shaders {
namespace RootSignatures {
namespace D3D12 {
// Luma's root parameters after the game's, in this order: LumaSettings and LumaData as root CBVs, every stage
inline constexpr uint32_t LUMA_PARAMETER_COUNT = 2;
// Then, when the signature has room and Luma has a space of its own, a table of Luma's textures (Resources::Bindings'
// D3D12_TEXTURE_REGISTERS SRVs from D3D12_FIRST_TEXTURE_REGISTER, in its space), pointing into the bound heap's tail
inline constexpr uint32_t LUMA_TABLE_PARAMETER = LUMA_PARAMETER_COUNT; // After the first of Luma's

struct Extended
{
   std::vector<uint8_t> blob;
   uint32_t luma_parameter = 0; // The index of the first of Luma's
   bool table = false; // Luma's texture table is at luma_parameter + LUMA_TABLE_PARAMETER
};

// Any thread: the game's serialized root signature with Luma's parameters, at its version. Empty when it can't be extended (over
// 64 DWORDs, Luma's registers already used, an unreadable blob), which is asserted once and logged.
Extended Extend(std::span<const uint8_t> blob);

// Any thread: a pipeline desc whose root signature comes with it rewritten to name the extended one: no root signature with one
// embedded in a shader (an RTS0 chunk), or a serialized root signature subobject. `desc` is of `kind`; the result is the new
// desc (the struct, or the stream) and the memory it points into. Empty when nothing changes.
struct RewrittenDesc
{
   std::vector<std::byte> bytes;
   std::vector<std::byte> stream; // A stream desc's stream, which `bytes` points to
};

std::optional<RewrittenDesc> RewritePipelineDesc(ID3D12Device* device, Database::D3D12::PipelineKind kind, const void* desc);

// The game released the device: the signatures created for its pipelines are released
void ReleaseDeviceObjects(ID3D12Device* device);
} // namespace D3D12
} // namespace RootSignatures
} // namespace Shaders
} // namespace Luma
