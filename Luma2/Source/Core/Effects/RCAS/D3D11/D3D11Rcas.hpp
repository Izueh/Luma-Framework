#pragma once

#include <d3d11.h>

namespace Luma {
namespace Effects {
namespace RCAS {
namespace D3D11 {
// Standalone RCAS sharpening pass using Luma_RCAS_PS.hlsl (`sharpness` in [0, 1]).
bool Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, float sharpness);
} // namespace D3D11
} // namespace RCAS
} // namespace Effects
} // namespace Luma
