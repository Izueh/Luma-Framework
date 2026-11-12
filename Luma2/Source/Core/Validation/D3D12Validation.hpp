#pragma once

#include <d3d12.h>

namespace Luma {
namespace Validation {
namespace D3D12 {
void BeforeCreateDevice();
void OnDeviceCreated(ID3D12Device* device);
// At every present
void ReadMessages();
void OnDeviceReleased(ID3D12Device* device);
} // namespace D3D12
} // namespace Validation
} // namespace Luma
