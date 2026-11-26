#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Hooks/HookFunctions.hpp"

struct IUnknown;

namespace Luma {
namespace Resources {
namespace Samplers {
struct Options
{
   bool upgrade = false;
   bool upgrade_comparison = true;
   bool force_upgrade_linear = false;
};

void SetOptions(const Options& options);
const Options& GetOptions();
void Register();
bool IsNeeded();
bool IsSwapping();
Hooks::D3D11::Functions GetD3D11Functions();

// Any thread: the device's offset (its super resolution's, or the Development custom one)
float GetLodBiasOffset(IUnknown* device);
// Any thread: changes with the device's offset and with the custom one
uint32_t GetGeneration(IUnknown* device);
// Presenting thread, at the device's present
void SetLodBiasOffset(IUnknown* device, float offset);
// For the dev tools: the offset of the device that presented last
float GetShownLodBiasOffset();

// Works directly on native D3D11_SAMPLER_DESC and D3D12_SAMPLER_DESC
template <typename D3DSamplerDesc>
std::optional<D3DSamplerDesc> GetMirrorDesc(const D3DSamplerDesc& desc, float offset)
{
   constexpr uint32_t FILTER_MIN_MAG_MIP_LINEAR = 0x15;
   constexpr uint32_t FILTER_ANISOTROPIC = 0x55;
   constexpr uint32_t FILTER_COMPARISON_ANISOTROPIC = 0xd5;
   constexpr uint32_t MAX_ANISOTROPY = 16;
   constexpr float MIN_LOD_BIAS = -16.f;
   constexpr float MAX_LOD_BIAS = 15.99f;
   const Options& opts = GetOptions();
   const uint32_t filter = uint32_t(desc.Filter);
   const bool upgraded = filter == FILTER_ANISOTROPIC || (opts.upgrade_comparison && filter == FILTER_COMPARISON_ANISOTROPIC) ||
      (opts.force_upgrade_linear && filter == FILTER_MIN_MAG_MIP_LINEAR);
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
