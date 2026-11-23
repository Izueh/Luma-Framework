#include "Display/DisplayComposition.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string_view>

#include "Display/DisplayMode.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Utils/Debug.hpp"

namespace Luma {
namespace Display {
namespace Composition {
namespace {
Options options;

// Written on the presenting thread before each composition, read by its cbuffer's write (the same thread, inside the draw)
std::mutex current_mutex;
CB::LumaCompositionData current = {};

constexpr std::array<std::string_view, size_t(Stage::Count)> STAGE_LABELS = {"Decode Stage", "UI Stage", "Gamma Correction Stage", "Gamut Mapping Stage", "Scale Stage", "Development Views Stage"};
constexpr std::array<std::string_view, size_t(Stage::Count)> STAGE_TOOLTIPS = {
   "Decodes the game's image to linear (gamma, sRGB). Off treats it as already linear.",
   "Composites separated UI over the scene at the UI paper white.",
   "Applies sRGB / gamma 2.2 mismatch correction.",
   "Maps out-of-gamut colors back into the target gamut.",
   "Scales the game's image to the real swapchain buffer size when they differ.",
   "Enables development views (debug draw, SDR/HDR split, 2x zoom).",
};

constexpr std::string_view DEV_CATEGORY = "Display Composition";

constexpr std::array<std::string_view, 5> GAMMA_CORRECTION_LABELS = {
   "sRGB",
   "2.2",
   "By Channel",
   "By Luma",
   "2.2 Luma",
};
constexpr std::array<std::string_view, 3> POST_PROCESS_SPACE_LABELS = {"SDR Gamma", "Linear", "Linear Until UI"};
constexpr std::array<std::string_view, 2> VANILLA_ENCODING_LABELS = {"sRGB", "Gamma 2.2"};
constexpr std::array<std::string_view, 4> GAMUT_MAPPING_LABELS = {"None", "Auto", "SDR (BT.709)", "HDR (BT.2020)"};
constexpr std::array<std::string_view, 4> UI_DRAW_TYPE_LABELS = {"Raw", "Direct Custom", "Direct Inverse", "Separate Texture"};
#if DEVELOPMENT
constexpr std::array<std::string_view, 5> SPLIT_VIEW_MODE_LABELS = {"Off", "Horizontal 2-way", "Horizontal 3-way", "Vertical 2-way", "Vertical 3-way"};
#endif

constexpr std::array USER_SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_GAMMA_CORRECTION.key,
      .label = "Gamma Correction",
      .tooltip = "Emulates a specific SDR transfer function in HDR.\n"
                 "sRGB - piecewise sRGB\n"
                 "Gamma 2.2 - pure 2.2 power curve\n"
                 "sRGB Hues (By Channel) - sRGB hues with gamma 2.2 luminance by channel\n"
                 "sRGB Hues (By Luminance) - sRGB hues with gamma 2.2 luminance by luminance\n"
                 "Gamma 2.2 Luminance - gamma 2.2 luminance with per-channel chrominance",
      .category = "Display",
      .default_value = Settings::EnumIndex{1},
      .enum_labels = GAMMA_CORRECTION_LABELS,
      .live = true,
      .is_visible = [] { return DisplayMode::IsHdr(); },
   },
};

constexpr std::array DEV_PARAM_SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_POST_PROCESS_SPACE.key,
      .label = "Post Process Space",
      .tooltip = "Encoding of the game's post-processing color buffers.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = POST_PROCESS_SPACE_LABELS,
      .live = true,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_EARLY_DISPLAY_ENCODING.key,
      .label = "Early Display Encoding",
      .tooltip = "Whether gamma correction and paper white scaling happen early in post processing (applies when Post Process Space is Linear).",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = false,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_VANILLA_ENCODING.key,
      .label = "Vanilla Encoding",
      .tooltip = "Transfer function the vanilla game wrote.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = VANILLA_ENCODING_LABELS,
      .live = true,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_GAMUT_MAPPING.key,
      .label = "Gamut Mapping",
      .tooltip = "Maps out-of-gamut colors back into the target gamut.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = GAMUT_MAPPING_LABELS,
      .live = true,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_UI_DRAW_TYPE.key,
      .label = "UI Draw Type",
      .tooltip = "How the game UI draws (Raw disables separate UI paper white).",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = UI_DRAW_TYPE_LABELS,
      .live = true,
   },
#if DEVELOPMENT
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_TEST_2X_ZOOM.key,
      .label = "Test 2x Zoom",
      .tooltip = "Zooms 2x into the image center for visual inspection.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = false,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_SPLIT_VIEW_MODE.key,
      .label = "SDR/HDR Split View",
      .tooltip = "Clamps a portion of the screen to SDR for side-by-side comparison.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = SPLIT_VIEW_MODE_LABELS,
      .live = true,
   },
   Settings::Setting{
      .key = Settings::Core::COMPOSITION_SPLIT_VIEW_NATIVE.key,
      .label = "Split View Native Impl",
      .tooltip = "Set when the game's tonemapper natively implements SDR/HDR split view instead of clipping in composition.",
      .category = DEV_CATEGORY,
      .visibility = Settings::Visibility::Development,
      .default_value = false,
   },
#endif
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
         .category = DEV_CATEGORY,
         .visibility = Settings::Visibility::Development,
         .default_value = true,
      };
   }
   return settings;
}

uint32_t GetStages()
{
   uint32_t stages = 0;
   for (size_t i = 0; i < size_t(Stage::Count); i++)
   {
      const bool on = Settings::Find(Settings::Core::COMPOSITION_STAGES[i].key) ? Settings::Get(Settings::Core::COMPOSITION_STAGES[i]) : true;
      stages |= on ? (1u << i) : 0u;
   }
   return stages;
}

void SyncToCBuffer()
{
   CB::DisplayCompositionSettings& dc = Resources::LumaCBuffers::cb_luma_global_settings.DisplayComposition;
   auto get_enum = [](const Settings::Id<Settings::EnumIndex>& id, int32_t fallback) -> uint32_t {
      return uint32_t(Settings::Find(id.key) ? Settings::Get(id).value : fallback);
   };
   auto get_bool = [](const Settings::Id<bool>& id, bool fallback) -> bool {
      return Settings::Find(id.key) ? Settings::Get(id) : fallback;
   };
   dc.PostProcessSpaceType = get_enum(Settings::Core::COMPOSITION_POST_PROCESS_SPACE, options.post_process_space);
   dc.VanillaEncodingType = get_enum(Settings::Core::COMPOSITION_VANILLA_ENCODING, options.vanilla_encoding);
   dc.GammaCorrectionType = get_enum(Settings::Core::COMPOSITION_GAMMA_CORRECTION, options.gamma_correction);
   dc.GamutMappingType = get_enum(Settings::Core::COMPOSITION_GAMUT_MAPPING, options.gamut_mapping);
   dc.UIDrawType = get_enum(Settings::Core::COMPOSITION_UI_DRAW_TYPE, options.ui_draw_type);
   dc.EarlyDisplayEncoding = get_bool(Settings::Core::COMPOSITION_EARLY_DISPLAY_ENCODING, options.early_display_encoding) ? 1u : 0u;
   dc.Stages = GetStages();
   uint32_t debug_flags = 0;
#if DEVELOPMENT
   if (get_bool(Settings::Core::COMPOSITION_TEST_2X_ZOOM, false))
      debug_flags |= 1u;
   debug_flags |= (get_enum(Settings::Core::COMPOSITION_SPLIT_VIEW_MODE, 0) & 7u) << 1u;
   if (get_bool(Settings::Core::COMPOSITION_SPLIT_VIEW_NATIVE, false))
      debug_flags |= 16u;
#endif
   dc.DebugFlags = debug_flags;
   Resources::LumaCBuffers::MarkSettingsDirty();
}

void ApplyOptionDefaults()
{
   if (Settings::Find(Settings::Core::COMPOSITION_POST_PROCESS_SPACE.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_POST_PROCESS_SPACE, Settings::EnumIndex{options.post_process_space});
   if (Settings::Find(Settings::Core::COMPOSITION_EARLY_DISPLAY_ENCODING.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_EARLY_DISPLAY_ENCODING, options.early_display_encoding);
   if (Settings::Find(Settings::Core::COMPOSITION_VANILLA_ENCODING.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_VANILLA_ENCODING, Settings::EnumIndex{options.vanilla_encoding});
   if (Settings::Find(Settings::Core::COMPOSITION_GAMMA_CORRECTION.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_GAMMA_CORRECTION, Settings::EnumIndex{options.gamma_correction});
   if (Settings::Find(Settings::Core::COMPOSITION_GAMUT_MAPPING.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_GAMUT_MAPPING, Settings::EnumIndex{options.gamut_mapping});
   if (Settings::Find(Settings::Core::COMPOSITION_UI_DRAW_TYPE.key))
      Settings::SetDefault(Settings::Core::COMPOSITION_UI_DRAW_TYPE, Settings::EnumIndex{options.ui_draw_type});
   SyncToCBuffer();
   DisplayMode::Refresh();
}

bool IsCompositionKey(std::string_view key)
{
   if (key == Settings::Core::COMPOSITION_GAMMA_CORRECTION.key)
      return true;
   for (const Settings::Setting& setting : DEV_PARAM_SETTINGS)
   {
      if (key == setting.key)
         return true;
   }
   for (const Settings::Id<bool>& stage : Settings::Core::COMPOSITION_STAGES)
   {
      if (key == stage.key)
         return true;
   }
   return false;
}

void Write(std::span<std::byte> destination, const Resources::Database::CustomData&, const Resources::Database::WriteTarget&)
{
   ASSERT_ONCE(destination.size() >= sizeof(CB::LumaCompositionData));
   std::scoped_lock lock(current_mutex);
   std::memcpy(destination.data(), &current, sizeof(current));
}

// Some of the game's shaders are replaced. Without variants, the mod's pipeline settings don't describe the image.
bool IsModActive()
{
   static uint64_t generation = UINT64_MAX;
   static bool any = false;
   if (const uint64_t now = Shaders::Variants::GetGeneration(); now != generation)
   {
      generation = now;
      any = !Shaders::Variants::GetAll().empty();
   }
   return Settings::IsEnabled() && Shaders::Variants::IsLoaded() && any;
}
} // namespace

void SetOptions(const Options& value)
{
   options = value;
   ApplyOptionDefaults();
}

const Options& GetOptions()
{
   return options;
}

void RegisterSettings()
{
   Settings::Register(USER_SETTINGS, Settings::Kind::Luma);
   Settings::Register(DEV_PARAM_SETTINGS, Settings::Kind::Development);
   static const std::array stage_settings = MakeStageSettings();
   Settings::Register(stage_settings, Settings::Kind::Development);
   ApplyOptionDefaults();
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value&) {
      if (!IsCompositionKey(setting.key))
         return;
      SyncToCBuffer();
      if (setting.key == Settings::Core::COMPOSITION_UI_DRAW_TYPE.key)
         DisplayMode::Refresh();
   });
}

void RegisterResources()
{
   // Found by the shader's cbuffer of that name
   Resources::Database::Register({.name_hash = Utils::Hash::Name("LumaComposition"),
      .name = "LumaComposition",
      .kind = Resources::Database::Kind::ConstantBuffer,
      .size = sizeof(CB::LumaCompositionData),
      .write = &Write});
}

std::optional<Pass> Plan(const Inputs& inputs)
{
   if (inputs.off)
      return std::nullopt;
   const bool mod_active = IsModActive();
   const CB::DisplayCompositionSettings& dc = Resources::LumaCBuffers::cb_luma_global_settings.DisplayComposition;
   const uint32_t stages = dc.Stages;

   if (!inputs.always)
   {
      const auto has_stage = [stages](Stage stage) { return (stages & (1u << uint32_t(stage))) != 0; };
      const bool sdr = Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 0;
      const bool input_linear = mod_active ? dc.PostProcessSpaceType == 1 : inputs.game_linear;
      const bool output_linear = inputs.encoding == Encoding::Linear;
      const bool needs_reencoding = (output_linear != input_linear) || inputs.encoding == Encoding::Pq ||
         (!mod_active && inputs.game_pq && inputs.encoding != Encoding::Pq);
      const bool early_display_encoding = dc.PostProcessSpaceType == 1 && dc.EarlyDisplayEncoding >= 1;
      const bool early_gamma_correction = early_display_encoding && dc.GammaCorrectionType < 2;
      const bool in_out_gamma_different = dc.VanillaEncodingType != dc.GammaCorrectionType;
      const bool display_mode_needs_gamma_correction = output_linear && !inputs.game_linear && sdr;
      const bool needs_gamma_correction = has_stage(Stage::GammaCorrection) &&
         ((mod_active ? (!early_gamma_correction && in_out_gamma_different) : in_out_gamma_different) || display_mode_needs_gamma_correction);
      const bool needs_paper_white_scaling = output_linear && !sdr && !early_display_encoding &&
         std::abs(Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits - System::Display::SRGB_WHITE_NITS) > 0.01f;
      const bool ui_needs_scaling = has_stage(Stage::UI) && mod_active && (dc.UIDrawType == 2 || dc.UIDrawType == 3);
      const bool needs_gamut_mapping = has_stage(Stage::GamutMapping) && mod_active && dc.GamutMappingType != 0;
      const bool needs_scaling = has_stage(Stage::Scale) && (inputs.source_scale.x != 1.f || inputs.source_scale.y != 1.f);
      const bool needs_dev_views = has_stage(Stage::DevelopmentViews) && (dc.DebugFlags != 0 || inputs.debug_draw_active);
      if (!needs_reencoding && !needs_gamma_correction && !needs_paper_white_scaling && !ui_needs_scaling && !needs_gamut_mapping && !needs_scaling &&
         !needs_dev_views)
         return std::nullopt;
   }

   Pass pass;
   // Shader fallback mode when Luma's shaders aren't active: 0 mod active, 1 vanilla gamma, 2 vanilla linear, 3 vanilla HDR10 PQ
   pass.custom.custom_data_1 = mod_active ? 0u : (inputs.game_pq ? 3u : (inputs.game_linear ? 2u : 1u));
   pass.data.OutputEncoding = uint32_t(inputs.encoding);
   pass.data.Stages = stages;
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
