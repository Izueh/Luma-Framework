#pragma once

#include <cstddef>
#include <span>

#include <d3d12.h>

#include "Common/ShaderStage.hpp"

namespace Luma {
namespace Resources {
namespace ConstantCapture {
namespace D3D12 {
// Reads the constant buffer bound at `slot` (space 0) of `stage` on `list` directly from its mapped upload heap.
// Returns false if not bound, not in an upload heap, or smaller than `destination`.
bool ReadBoundCBuffer(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot, std::span<std::byte> destination);
} // namespace D3D12
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
