#pragma once

#include <d3d11_4.h>

// Resources::Samplers on D3D11: each game sampler keeps, in its private data, its mirror for the current LOD bias offset (made at the
// first bind after the offset changed; the older ones are kept, a switch back reuses them, they die with the sampler)
namespace Luma {
namespace Resources {
namespace Samplers {
namespace D3D11 {
// At the game's sampler creation
void TrackSampler(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler);
// Binds, while swapping: each tracked sampler in the detour's copy of the array replaced by its mirror
void SwapSamplers(ID3D11SamplerState** samplers, UINT count);
} // namespace D3D11
} // namespace Samplers
} // namespace Resources
} // namespace Luma
