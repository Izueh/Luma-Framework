#pragma once

#include <d3d12.h>

#include "Effects/SR/SuperResolution.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
void CreateDeviceObjects(ID3D12Device* device);
void ReleaseDeviceObjects(ID3D12Device* device);
void OnPresent(ID3D12Device* device);

// Runs the active native D3D12 SR backend using inputs/output tagged in Resources::Database (Sr::*) and camera/resolutions in Frame.
// Transitions resources, handles UAV temp output + copy-back automatically, restores tracked command list state, and marks Frame::PassMarker::Sr.
bool Draw(ID3D12GraphicsCommandList* list, const DrawOverrides& overrides = {});
Type GetActiveType(ID3D12Device* device);
int GetJitterPhases(ID3D12Device* device);
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
