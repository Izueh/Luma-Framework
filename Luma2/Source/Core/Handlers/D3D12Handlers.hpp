#pragma once

#include <d3d12.h>

namespace Luma {
namespace Handlers {
// Enables the debug layer for the device about to be created, when this build asks for it
void PreCreateDevice();
void PostCreateDevice(ID3D12Device* device);
// The game released its last reference. The device is still valid.
void OnDeviceDestroyed(ID3D12Device* device);
// Every submission, Luma's own included
void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
} // namespace Handlers
} // namespace Luma
