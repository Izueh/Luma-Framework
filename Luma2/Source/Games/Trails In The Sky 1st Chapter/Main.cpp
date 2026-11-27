#include "Game/Game.hpp"
#include "Luma.hpp"

#include <array>
#include <string_view>

using namespace Luma;

namespace {
using Game::cb_luma_global_settings;
using Settings::EnumIndex;
using Settings::Range;
using Settings::Setting;
using Settings::Visibility;
using Settings::Sources::Binding;

constexpr std::array<std::string_view, 2> TONEMAP_TYPES = {"CInfinity", "PsychoV 35"};

constexpr std::string_view DESCRIPTION =
   "Luma for \"Trails in the Sky 1st Chapter\".\n"
   "The game's HDR output follows Luma's display settings: the image keeps the game's look up to paper white and only the highlights above "
   "it roll off to the display's peak. The TAA keeps the highlights it used to clip, and the bloom adds light instead of screen blending.";
constexpr std::array<std::string_view, 3> CREDITS = {
   "Third Party:",
   "ImGui",
   "Luma Framework",
};

constexpr std::array<uint32_t, 1> SHADER_HASHES_FINAL = {0xE20E1A41};
constexpr std::array<uint32_t, 1> SHADER_HASHES_FINAL_SDR = {0x14DAB5E7};
constexpr std::array<uint32_t, 1> SHADER_HASHES_SCENE_COPY = {0xC9FA40B7};
constexpr std::array<uint32_t, 1> SHADER_HASHES_TAA = {0xFA37EA04};
constexpr std::array<uint32_t, 1> SHADER_HASHES_GLOW = {0xD6CF040B};
constexpr std::array<uint32_t, 1> SHADER_HASHES_GODRAY_SOURCE = {0xD0B53A29};
constexpr std::array<uint32_t, 1> SHADER_HASHES_GODRAY = {0x1343A4D6};
constexpr std::array<uint32_t, 1> SHADER_HASHES_SKY = {0xA663A400};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AUTO_EXPOSURE_LUMA = {0x8916CA39};

const std::array SETTINGS = {
   Setting{
      .key = "SunIntensityBoost",
      .label = "Sun Intensity Boost",
      .tooltip = "Multiplier on the procedural sky's isolated sun disk term in HDR mode.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.75f,
      .range = Range{0.5f, 4.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.sun_intensity_boost},
      .live = true,
   },
   Setting{
      .key = "HDRBloomScale",
      .label = "HDR Bloom Scale",
      .tooltip = "Linear-space additive bloom strength for HDR highlights above 1.0.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 0.25f,
      .range = Range{0.0f, 1.5f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.hdr_bloom_scale},
      .live = true,
   },
   Setting{
      .key = "SDRHueRestore",
      .label = "SDR Hue Restore",
      .tooltip = "Strength of MacLeod-Boynton hue restoration from the clipped SDR bloom reference.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.0f,
      .range = Range{0.0f, 1.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.sdr_hue_restore},
      .live = true,
   },
   Setting{
      .key = "SDRPurityRestore",
      .label = "SDR Purity Restore",
      .tooltip = "Strength of MacLeod-Boynton purity restoration from the clipped SDR bloom reference.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 0.85f,
      .range = Range{0.0f, 1.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.sdr_purity_restore},
      .live = true,
   },
   Setting{
      .key = "GodraySourcePeak",
      .label = "Godray Source Peak",
      .tooltip = "Soft-cap peak on extracted godray source brightness before radial blur.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.2f,
      .range = Range{0.85f, 3.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.godray_source_peak},
      .live = true,
   },
   Setting{
      .key = "HDRGodrayScale",
      .label = "HDR Godray Scale",
      .tooltip = "Linear-space additive godray strength for HDR highlights above 1.0.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 0.35f,
      .range = Range{0.0f, 1.5f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.hdr_godray_scale},
      .live = true,
   },
   Setting{
      .key = "RolloffAnchor",
      .label = "Rolloff Anchor",
      .tooltip = "Linear luminance (1.0 = paper white) where CInfinityRollOff begins compressing highlights.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.0f,
      .range = Range{0.18f, 1.5f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.rolloff_anchor},
      .live = true,
   },
   Setting{
      .key = "RolloffCompression",
      .label = "Rolloff Compression",
      .tooltip = "CInfinityRollOff / PsychoV 35 compression strength above the anchor (>= 1.0).",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.25f,
      .range = Range{1.0f, 3.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.rolloff_compression},
      .live = true,
   },
   Setting{
      .key = "TonemapType",
      .label = "Tonemapper",
      .tooltip = "Display mapper / tonemapper used in SceneCopy before UI.",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = EnumIndex{1},
      .enum_labels = TONEMAP_TYPES,
      .source = Binding{&cb_luma_global_settings.GameSettings.tonemap_type},
      .live = true,
   },
   Setting{
      .key = "CInfinityBlowout",
      .label = "CInfinity Highlight Blowout",
      .tooltip = "Blends CInfinity MaxChannel (0.0) toward hue-preserving per-channel blowout to white (1.0).",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 0.35f,
      .range = Range{0.0f, 1.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.cinfinity_blowout},
      .live = true,
   },
   Setting{
      .key = "PsychoDechroma",
      .label = "PsychoV 35 Dechroma (Blowout)",
      .tooltip = "PsychoV 35 highlight desaturation / blowout toward white (0.0 - 1.0).",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 0.35f,
      .range = Range{0.0f, 1.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.psycho_dechroma},
      .live = true,
   },
   Setting{
      .key = "PsychoHighlightSaturation",
      .label = "PsychoV 35 Highlight Saturation",
      .tooltip = "PsychoV 35 highlight saturation multiplier (1.0 = neutral, <1.0 = softer blowout).",
      .category = "HDR Lighting",
      .visibility = Visibility::Test,
      .default_value = 1.0f,
      .range = Range{0.0f, 2.0f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.psycho_highlight_saturation},
      .live = true,
   },
};
} // namespace

// Runs under the loader lock: declarations only (no DLL loads, threads, or D3D objects).
void Luma::Game::Init()
{
   SetInfo({
      .version = "2.0.0",
      .description = DESCRIPTION,
      .credits = CREDITS,
   });

   Shaders::SetDebugName("Final", SHADER_HASHES_FINAL);
   Shaders::SetDebugName("Final (SDR)", SHADER_HASHES_FINAL_SDR);
   Shaders::SetDebugName("Scene Copy", SHADER_HASHES_SCENE_COPY);
   Shaders::SetDebugName("TAA", SHADER_HASHES_TAA);
   Shaders::SetDebugName("Glow", SHADER_HASHES_GLOW);
   Shaders::SetDebugName("Godray Source", SHADER_HASHES_GODRAY_SOURCE);
   Shaders::SetDebugName("Godray", SHADER_HASHES_GODRAY);
   Shaders::SetDebugName("Sky", SHADER_HASHES_SKY);
   Shaders::SetDebugName("AutoExposure Luma", SHADER_HASHES_AUTO_EXPOSURE_LUMA);

   Settings::Register(SETTINGS);

   // Final (SDR) outputs linear RGB (1.0 = GamePaperWhiteNits); Luma Display Composition handles paper white scaling and
   // HDR10 / scRGB / SDR-on-HDR / SDR swapchain encoding.
   SetOptions({
      .cbuffers = {.settings_slot = 13, .data_slot = 12},
      .swapchain = {.mode = Swapchain::Mode::Mirror, .hdr_output = Swapchain::OutputMode::Hdr10},
      .composition = {
         .post_process_space = 1,
         .early_display_encoding = false,
         .vanilla_encoding = 1,
         .gamma_correction = 1,
         .ui_draw_type = 1,
      },
      .supported_apis = {Api::D3D11},
   });

   // The TAA's history is an 8 bit copy of its output (0xA22BF9AA into R8G8B8A8), which clips the highlights the patched TAA keeps, so
   // the game's render targets are mirrored in 16 bit float
   Resources::SetUpgrades({
      .formats = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM,
         DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
         DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_R11G11B10_FLOAT},
      .size_filters = Resources::Upgrade::SizeFilters::SwapchainResolution | Resources::Upgrade::SizeFilters::SwapchainAspectRatio,
   });
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   return Luma::DllMain(module, reason, reserved);
}
