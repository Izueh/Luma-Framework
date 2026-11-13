#pragma once

#include <d3d11.h>

namespace Luma {
namespace Validation {
namespace D3D11 {
UINT GetCreateFlags(UINT flags);
void WatchDeviceMessages(ID3D11Device* device);
// At every present
void ReadMessages();
void StopWatchingDevice(ID3D11Device* device);
} // namespace D3D11
} // namespace Validation
} // namespace Luma
