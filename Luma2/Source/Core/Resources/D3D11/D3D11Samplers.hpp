#pragma once

#include <d3d11_4.h>

namespace Luma {
namespace Resources {
namespace Samplers {
namespace D3D11 {
void TrackSampler(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler);
// Presenting thread
void RefreshMirrors(ID3D11Device* device);
void SwapSamplers(ID3D11SamplerState** samplers, UINT count);
} // namespace D3D11
} // namespace Samplers
} // namespace Resources
} // namespace Luma
