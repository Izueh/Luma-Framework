#pragma once

#include <d3d12.h>

namespace Luma {
namespace Handlers {
void OnDeviceCreated(ID3D12Device* device);
// The game released its last reference. The device is still valid.
void OnDeviceReleased(ID3D12Device* device);
} // namespace Handlers
} // namespace Luma
