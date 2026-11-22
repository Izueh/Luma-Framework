#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Hooks/Functions.hpp"

namespace Luma {
namespace Resources {
namespace Samplers {
struct Options
{
   bool upgrade = false;
};

void SetOptions(const Options& options);
const Options& GetOptions();
void Register();
bool IsNeeded();
bool IsSwapping();
Hooks::D3D11::Functions GetD3D11Functions();

float GetLodBiasOffset();
uint32_t GetGeneration();
void SetLodBiasOffset(float offset);

// Works directly on native D3D11_SAMPLER_DESC and D3D12_SAMPLER_DESC
template <typename D3DSamplerDesc>
std::optional<D3DSamplerDesc> GetMirrorDesc(const D3DSamplerDesc& desc, float offset = GetLodBiasOffset())
{
   constexpr uint32_t FILTER_MIN_MAG_MIP_LINEAR = 0x15;
   constexpr uint32_t FILTER_ANISOTROPIC = 0x55;
   constexpr uint32_t FILTER_COMPARISON_ANISOTROPIC = 0xd5;
   constexpr uint32_t MAX_ANISOTROPY = 16;
   constexpr float MIN_LOD_BIAS = -16.f;
   constexpr float MAX_LOD_BIAS = 15.99f;
   constexpr bool UPGRADE_COMPARISON_SAMPLERS = true;
   constexpr bool FORCE_UPGRADE_LINEAR_SAMPLERS = false;

   const uint32_t filter = uint32_t(desc.Filter);
   const bool upgraded = filter == FILTER_ANISOTROPIC || (UPGRADE_COMPARISON_SAMPLERS && filter == FILTER_COMPARISON_ANISOTROPIC) ||
      (FORCE_UPGRADE_LINEAR_SAMPLERS && filter == FILTER_MIN_MAG_MIP_LINEAR);
   if (!upgraded)
      return std::nullopt;
   D3DSamplerDesc mirror = desc;
   if (filter == FILTER_MIN_MAG_MIP_LINEAR)
      mirror.Filter = decltype(desc.Filter)(FILTER_ANISOTROPIC);
   mirror.MaxAnisotropy = MAX_ANISOTROPY;
   mirror.MipLODBias = std::clamp(desc.MipLODBias + offset, MIN_LOD_BIAS, MAX_LOD_BIAS);
   const float difference = mirror.MipLODBias - desc.MipLODBias;
   mirror.MinLOD = std::max(desc.MinLOD + std::min(difference, 0.f), 0.f);
   if (mirror.Filter == desc.Filter && mirror.MaxAnisotropy == desc.MaxAnisotropy && mirror.MipLODBias == desc.MipLODBias && mirror.MinLOD == desc.MinLOD)
      return std::nullopt;
   return mirror;
}
} // namespace Samplers
} // namespace Resources
} // namespace Luma
