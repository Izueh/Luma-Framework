#pragma once

#include <cstdint>
#include <iterator>

namespace Luma {
namespace Resources {
namespace Bindings {
struct Options
{
   uint32_t d3d12_space = 100; // 0 falls back to space 0 at D3D11's slots
   bool static_samplers = false; // Opt-in: alters every extended root signature
};

// Top registers of Luma's space, matching DXP's reverse_bind
inline constexpr uint32_t D3D12_TEXTURE_REGISTERS = 16;
inline constexpr uint32_t D3D12_FIRST_TEXTURE_REGISTER = 128 - D3D12_TEXTURE_REGISTERS;

// Order matches s0..s4 in Luma's space
enum class StaticSampler : uint32_t
{
   PointClamp,
   LinearClamp,
   LinearWrap,
   AnisotropicWrap,
   PointWrap,
};

inline constexpr const char* D3D12_STATIC_SAMPLER_DEFINES[] = {"LUMA_POINT_CLAMP_SAMPLER_INDEX", "LUMA_LINEAR_CLAMP_SAMPLER_INDEX",
   "LUMA_LINEAR_WRAP_SAMPLER_INDEX", "LUMA_ANISOTROPIC_WRAP_SAMPLER_INDEX", "LUMA_POINT_WRAP_SAMPLER_INDEX"};
inline constexpr uint32_t D3D12_STATIC_SAMPLERS = uint32_t(std::size(D3D12_STATIC_SAMPLER_DEFINES));
static_assert(uint32_t(StaticSampler::PointWrap) + 1 == D3D12_STATIC_SAMPLERS);

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
} // namespace Bindings
} // namespace Resources
} // namespace Luma
