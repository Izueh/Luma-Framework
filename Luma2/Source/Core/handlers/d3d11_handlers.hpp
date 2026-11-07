#pragma once

#include <d3d11.h>

namespace Luma {
namespace Handlers {
void OnDeviceCreated(ID3D11Device* device);
// The game released its last reference. The device is still valid.
void OnDeviceReleased(ID3D11Device* device);
} // namespace Handlers
} // namespace Luma
