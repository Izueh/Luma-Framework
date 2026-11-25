#pragma once

#include <d3d11.h>

#include "Effects/SR/SuperResolution.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
void PreCreateDevice(const D3D_FEATURE_LEVEL*& feature_levels, UINT& feature_level_count);
void CreateDeviceObjects(ID3D11Device* device);
void ReleaseDeviceObjects(ID3D11Device* device);
void OnPresent(ID3D11Device* device);

// Runs the active SR backend using inputs/output tagged in Resources::Database (Sr::*) and camera/resolutions in Frame.
// Saves/restores graphics+compute state, handles UAV temp output + copy-back automatically, and marks Frame::PassMarker::Sr.
bool Draw(ID3D11DeviceContext* context, const DrawOverrides& overrides = {});
Type GetActiveType(ID3D11Device* device);
int GetJitterPhases(ID3D11Device* device);
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
