#include "Resources/Samplers.hpp"

#include <algorithm>
#include <array>
#include <atomic>

#include "Luma.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"

namespace Luma {
namespace Resources {
namespace Samplers {
namespace {
Options options;
std::atomic<bool> swapping = true;
std::atomic<float> sr_offset = 0.f;
std::atomic<bool> custom_bias = false;
std::atomic<float> custom_offset = 0.f;
std::atomic<uint32_t> generation = 0;

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::SAMPLERS.key,
      .label = "Sampler Upgrades",
      .tooltip = "Luma's 16x anisotropic copies of the game's anisotropic samplers, with the LOD bias super resolution needs, in place of the game's.",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = true,
   },
};

constexpr std::array DEVELOPMENT_SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::SAMPLERS_CUSTOM_BIAS.key,
      .label = "Custom Sampler LOD Bias",
      .tooltip = "Replaces the LOD bias offset super resolution sets (1.0's custom_texture_mip_lod_bias_offset)",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = false,
   },
   Settings::Setting{
      .key = Settings::Core::SAMPLERS_BIAS.key,
      .label = "Sampler LOD Bias Offset",
      .tooltip = "Added to the upgraded samplers' LOD bias while Custom Sampler LOD Bias is on. Negative sharpens.",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = 0.f,
      .range = Settings::Range{-4.f, 4.f},
      .format = "%.2f",
   },
};

void Bump()
{
   generation.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void SetOptions(const Options& value)
{
   options = value;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
#if DEVELOPMENT
   Settings::Register(DEVELOPMENT_SETTINGS, Settings::Kind::Development);
#endif
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::SAMPLERS.key)
         swapping = std::get<bool>(value);
      else if (setting.key == Settings::Core::SAMPLERS_CUSTOM_BIAS.key)
      {
         custom_bias = std::get<bool>(value);
         Bump();
      }
      else if (setting.key == Settings::Core::SAMPLERS_BIAS.key)
      {
         custom_offset = std::get<float>(value);
         Bump();
      }
   });
}

bool IsNeeded()
{
   return options.upgrade;
}

bool IsSwapping()
{
   return IsEnabled() && swapping.load(std::memory_order_relaxed);
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!IsNeeded())
      return functions;
   for (const Hooks::D3D11::Function function : {CreateSamplerState, VSSetSamplers, HSSetSamplers, DSSetSamplers, GSSetSamplers, PSSetSamplers, CSSetSamplers})
      functions.set(size_t(function));
   return functions;
}

float GetLodBiasOffset()
{
   return custom_bias.load(std::memory_order_relaxed) ? custom_offset.load(std::memory_order_relaxed) : sr_offset.load(std::memory_order_relaxed);
}

uint32_t GetGeneration()
{
   return generation.load(std::memory_order_relaxed);
}

void SetLodBiasOffset(float offset)
{
   if (sr_offset.exchange(offset) != offset)
      Bump();
}
} // namespace Samplers
} // namespace Resources
} // namespace Luma
