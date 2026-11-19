#include "Display/Composition.hpp"

#include <charconv>
#include <cstring>
#include <mutex>
#include <array>
#include <string>
#include <string_view>

#include "Luma.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Defines.hpp"
#include "Shaders/Variants.hpp"
#include "Utils/Debug.hpp"

namespace Luma {
namespace Display {
namespace Composition {
namespace {
// Written on the presenting thread before each composition, read by its cbuffer's write (the same thread, inside the draw)
std::mutex current_mutex;
CB::LumaCompositionData current = {};

void Write(std::span<std::byte> destination, const Resources::Database::CustomData&, const Resources::Database::WriteTarget&)
{
   ASSERT_ONCE(destination.size() >= sizeof(CB::LumaCompositionData));
   std::scoped_lock lock(current_mutex);
   std::memcpy(destination.data(), &current, sizeof(current));
}

// The value a define was last built with (1.0's GetShaderDefineCompiledNumericalValue), the shaders' default before the first build
template <typename T>
int32_t GetCompiled(const Shaders::Defines::Id<T>& define, int32_t fallback)
{
   const std::optional<std::string> text = Shaders::Defines::GetCompiledText(define.name);
   int32_t value = fallback;
   if (text)
      std::from_chars(text->data(), text->data() + text->size(), value);
   return value;
}

// 1.0's IsModActive: some of the game's shaders are replaced. Without, the defines don't describe the image.
bool IsModActive()
{
   static uint64_t generation = UINT64_MAX;
   static bool any = false;
   if (const uint64_t now = Shaders::Variants::GetGeneration(); now != generation)
   {
      generation = now;
      any = !Shaders::Variants::GetAll().empty();
   }
   return IsEnabled() && Shaders::Variants::IsLoaded() && any;
}
} // namespace

constexpr std::array<std::string_view, size_t(Stage::Count)> STAGE_LABELS = {"Decode", "UI", "Gamma Correction", "Gamut Mapping", "Scale", "Development Views"};
constexpr std::array<std::string_view, size_t(Stage::Count)> STAGE_TOOLTIPS = {
   "The game's image decoded to linear (gamma, sRGB). Off takes it as linear already.",
   "The separated UI composited over the scene, at the UI paper white",
   "The sRGB and gamma 2.2 mismatch corrected (GAMMA_CORRECTION_TYPE)",
   "Colors out of the output's gamut brought back in (GAMUT_MAPPING_TYPE)",
   "The game's image scaled to the real buffer's size, when they differ",
   "Debug draw of a captured view, the SDR/HDR split view",
};

std::array<Settings::Setting, size_t(Stage::Count)> MakeStageSettings()
{
   std::array<Settings::Setting, size_t(Stage::Count)> settings;
   for (size_t i = 0; i < settings.size(); i++)
   {
      settings[i] = {
         .key = Settings::Core::COMPOSITION_STAGES[i].key,
         .label = STAGE_LABELS[i],
         .tooltip = STAGE_TOOLTIPS[i],
         .category = "Composition",
         .visibility = Stage(i) == Stage::DevelopmentViews ? Settings::Visibility::Development : Settings::Visibility::Test,
         .default_value = true,
      };
   }
   return settings;
}

// Read at every composition (atomics)
uint32_t GetStages()
{
   uint32_t stages = 0;
   for (size_t i = 0; i < size_t(Stage::Count); i++)
      stages |= Settings::Get(Settings::Core::COMPOSITION_STAGES[i]) ? (1u << i) : 0u;
   return stages;
}

void Register()
{
   static const std::array settings = MakeStageSettings();
   Settings::Register(settings, Settings::Kind::Luma);
   // Found by the shader's cbuffer of that name
   Resources::Database::Register({.name_hash = Utils::Hash::Name("LumaComposition"),
      .name = "LumaComposition",
      .kind = Resources::Database::Kind::ConstantBuffer,
      .size = sizeof(CB::LumaCompositionData),
      .write = &Write});
}

std::optional<Pass> Plan(const Inputs& inputs)
{
   namespace Defines = Shaders::Defines;
   const bool mod_active = IsModActive();
   const int32_t post_process_space = GetCompiled(Defines::POST_PROCESS_SPACE_TYPE, 0);
   const int32_t gamma_correction = GetCompiled(Defines::GAMMA_CORRECTION_TYPE, 1);
   const bool sdr = Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 0;
   // 1.0's needs, from its OnPresent. Its output was a scRGB swapchain, or the game's linear one; an encode is a reencoding here.
   const bool input_linear = mod_active ? post_process_space == 1 : inputs.game_linear;
   const bool needs_reencoding = inputs.encoding != Encoding::Linear || !input_linear;
   const bool early_display_encoding = post_process_space == 1 && GetCompiled(Defines::EARLY_DISPLAY_ENCODING, 0) >= 1;
   const bool early_gamma_correction = early_display_encoding && gamma_correction < 2;
   const bool in_out_gamma_different = GetCompiled(Defines::VANILLA_ENCODING_TYPE, 0) != gamma_correction;
   const bool display_mode_needs_gamma_correction = !inputs.game_linear && sdr;
   const bool needs_gamma_correction = (mod_active ? (!early_gamma_correction && in_out_gamma_different) : in_out_gamma_different) || display_mode_needs_gamma_correction;
   const bool ui_needs_scaling = mod_active && GetCompiled(Defines::UI_DRAW_TYPE, 0) == 2;
   const bool needs_gamut_mapping = mod_active && GetCompiled(Defines::GAMUT_MAPPING_TYPE, 0) != 0;
   const bool needs_scaling = inputs.source_scale.x != 1.f || inputs.source_scale.y != 1.f;
   if (!inputs.always && !needs_reencoding && !needs_gamma_correction && !ui_needs_scaling && !needs_gamut_mapping && !needs_scaling)
      return std::nullopt;

   Pass pass;
   // The shader's linearization without Luma's shaders: 1 from gamma, 2 already linear (0 is the mod's defines)
   pass.custom.custom_data_1 = mod_active ? 0 : (inputs.game_linear ? 2 : 1);
   pass.data.OutputEncoding = uint32_t(inputs.encoding);
   pass.data.Stages = GetStages();
   pass.data.SourceScale = inputs.source_scale;
   return pass;
}

void SetCurrent(const Pass& pass)
{
   std::scoped_lock lock(current_mutex);
   current = pass.data;
}
} // namespace Composition
} // namespace Display
} // namespace Luma
