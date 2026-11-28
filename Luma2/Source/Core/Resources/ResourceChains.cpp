#include "Resources/ResourceChains.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>

#include "Game/GameRegistry.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

namespace Luma {
namespace Resources {
namespace Chains {
namespace {
Options options;
std::atomic<uint8_t> active_mode = uint8_t(Mode::None);
std::atomic<uint32_t> direct_propagations = 0;
std::atomic<uint32_t> indirect_propagations = 0;
std::atomic<int32_t> deferred_queued = 0;

constexpr std::array<std::string_view, 3> MODE_LABELS = {"Off", "Direct", "Direct & Indirect"};

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::CHAINS.key,
      .label = "Upgrade Chains",
      .tooltip = "Propagates texture mirrors across copies (Direct) or also across draws and dispatches reading a mirror (Direct & Indirect).",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = MODE_LABELS,
      .is_visible = [] { return Game::Registry::HasSupportedApi({Api::D3D11, Api::D3D12}) && IsNeeded(); },
   },
};
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::CHAINS.key)
         active_mode.store(uint8_t(std::clamp(std::get<Settings::EnumIndex>(value).value, 0, 2)), std::memory_order_relaxed);
   });
}

void SetOptions(const Options& value)
{
   options = value;
   active_mode.store(uint8_t(options.mode), std::memory_order_relaxed);
   Settings::SetDefault(Settings::Core::CHAINS, Settings::EnumIndex{int32_t(options.mode)});
}

const Options& GetOptions()
{
   return options;
}

Mode GetMode()
{
   return Mode(active_mode.load(std::memory_order_relaxed));
}

bool IsNeeded()
{
   return options.mode != Mode::None || !options.seeds.empty();
}

const Seed* FindSeed(uint32_t shader_hash)
{
   if (options.seeds.empty())
      return nullptr;
   const auto it = options.seeds.find(shader_hash);
   return it != options.seeds.end() ? &it->second : nullptr;
}

bool HasScaleSeeds()
{
   return std::ranges::any_of(options.seeds, [](const auto& pair) { return pair.second.scale; });
}

bool CanPropagate(const Upgrade::TextureInfo& source, const Upgrade::TextureInfo& target, uint32_t aspect_ratio_pixel_threshold)
{
   if (target.dimension != Upgrade::Dimension::Texture2D || !target.gpu_only)
      return false;
   if (Utils::Format::DXGI::AreCopyCompatible(source.format, target.format))
      return false;
   if (Utils::Format::DXGI::IsInteger(target.format))
      return false;
   if (Utils::Format::DXGI::IsRgba(source.format, true) != Utils::Format::DXGI::IsRgba(target.format, true))
      return false;
   if (source.width == 0 || source.height == 0 || target.width == 0 || target.height == 0)
      return false;
   if (source.width == target.width && source.height == target.height && source.depth_or_layers == target.depth_or_layers)
      return true;
   if (source.dimension != Upgrade::Dimension::Texture2D || target.depth_or_layers != 1 || target.cube || (target.width == 1 && target.height == 1))
      return false;
   const float threshold = float(aspect_ratio_pixel_threshold);
   const float width = float(target.width);
   const float height = float(target.height);
   const float min_ratio = target.width <= target.height ? (width - threshold) / height : width / (height + threshold);
   const float max_ratio = target.width <= target.height ? (width + threshold) / height : width / (height - threshold);
   const float source_ratio = float(source.width) / float(source.height);
   return source_ratio >= min_ratio - FLT_EPSILON && source_ratio <= max_ratio + FLT_EPSILON;
}

std::string_view GetModeName(Mode mode)
{
   switch (mode)
   {
   case Mode::Direct: return "Direct";
   case Mode::DirectAndIndirect: return "Direct & Indirect";
   default: return "Off";
   }
}

std::string_view GetOriginName(OriginKind kind)
{
   switch (kind)
   {
   case OriginKind::Rule: return "Rule";
   case OriginKind::CustomDimensions: return "CustomDim";
   case OriginKind::Seed: return "Seed";
   case OriginKind::DirectChain: return "DirectChain";
   case OriginKind::IndirectChain: return "IndirectChain";
   default: return "Unknown";
   }
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!IsNeeded())
      return functions;
   if (options.mode >= Mode::DirectAndIndirect || !options.seeds.empty())
   {
      for (const Hooks::D3D11::Function function : {CreatePixelShader, CreateComputeShader, PSSetShader, CSSetShader, Draw, DrawInstanced, DrawIndexed,
              DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect, ClearState, FinishCommandList,
              ExecuteCommandList})
         functions.set(size_t(function));
   }
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   if (!IsNeeded())
      return functions;
   if (options.mode >= Mode::DirectAndIndirect || !options.seeds.empty())
   {
      for (const Hooks::D3D12::Function function : {CreatePipelineState, SetPipelineState, Draw, Dispatch, ExecuteIndirect, Reset, ClearState, ResourceBarrier})
         functions.set(size_t(function));
   }
   return functions;
}

void CountDirectPropagation()
{
   direct_propagations.fetch_add(1, std::memory_order_relaxed);
}

void CountIndirectPropagation()
{
   indirect_propagations.fetch_add(1, std::memory_order_relaxed);
}

void CountDeferredQueued(int32_t delta)
{
   deferred_queued.fetch_add(delta, std::memory_order_relaxed);
}

Stats GetStats()
{
   return {
      .mode = GetMode(),
      .seeds = uint32_t(options.seeds.size()),
      .direct_propagations = direct_propagations.load(std::memory_order_relaxed),
      .indirect_propagations = indirect_propagations.load(std::memory_order_relaxed),
      .deferred_queued = uint32_t(std::max<int32_t>(deferred_queued.load(std::memory_order_relaxed), 0)),
   };
}
} // namespace Chains
} // namespace Resources
} // namespace Luma
