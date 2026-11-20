#include "Luma.hpp"

#include <array>
#include <chrono>
#include <random>
#include <string_view>

#include <wrl/client.h>

// The fields UpdateLumaInstanceDataCB reads. TAA and SR will set them; until then they keep their swapchain values.
struct Luma::Game::GameDeviceData
{
   Luma::Math::float2 upscaled_render_resolution = {1.f, 1.f};
   float resolution_scale = 1.f;
   Luma::Math::uint4 viewport_rect = {0, 0, 1, 1};
};

namespace {
using Luma::operator""_h;
using Luma::Game::cb_luma_global_settings;
using Luma::Game::GameDeviceData;

// The About text and credits
constexpr std::string_view DESCRIPTION = "Luma for \"Final Fantasy VII Remake\" is developed by Izueh and Pumbo and is open source and free. It adds DLSS and improved HDR "
                                         "tonemapping. Additional thanks to ShortFuse and Musa from the RenoDX team and their HDR mods for Remake and Rebirth, which served "
                                         "as reference. If you enjoy it, consider donating to any of the contributors.";
constexpr std::array<std::string_view, 12> CREDITS = {
   "Main:",
   "Izueh",
   "Pumbo",
   "Acknowledgments:",
   "ShortFuse",
   "Musa",
   "Third Party:",
   "ImGui",
   "RenoDX",
   "3Dmigoto",
   "Oklab",
   "DICE (HDR tonemapper)",
};
constexpr std::array<Luma::Game::Link, 5> LINKS = {{
   {"Buy Izueh a Coffee on ko-fi", "https://ko-fi.com/izueh"},
   {"Buy Pumbo a Coffee on buymeacoffee", "https://buymeacoffee.com/realfiloppi"},
   {"Buy Pumbo a Coffee on ko-fi", "https://ko-fi.com/realpumbo"},
   // Unique link for Luma by Pumbo (to track where people join from), not to be used for anything else
   {"Join our \"HDR Den\" Discord", "https://discord.gg/J9fM3EVuEZ"},
   {"Contribute on Github", "https://github.com/Filoppi/Luma-Framework"},
}};

// The game's shaders by hash, each with a debug name (Init)
constexpr std::array<uint32_t, 1> SHADER_HASHES_TAA = {0x4729683B};
constexpr std::array<uint32_t, 1> SHADER_HASHES_TITLE = {0x5FEE74F9};
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOF = {0xB400FAF6};
constexpr std::array<uint32_t, 1> SHADER_HASHES_MOTION_BLUR = {0xB0F56393};
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOWNSAMPLE_BLOOM = {0x2174B927};
constexpr std::array<uint32_t, 1> SHADER_HASHES_BLOOM = {0x4D6F937E};
constexpr std::array<uint32_t, 1> SHADER_HASHES_MENU_SLOWDOWN = {0x968B821F};
constexpr std::array<uint32_t, 1> SHADER_HASHES_TONEMAP = {0xF68D39B5};
constexpr std::array<uint32_t, 1> SHADER_HASHES_VELOCITY_FLATTEN = {0x4EB2EA5B};
constexpr std::array<uint32_t, 1> SHADER_HASHES_VELOCITY_GATHER = {0xFEE03685};
constexpr std::array<uint32_t, 1> SHADER_HASHES_SSAO_TEMPORAL = {0x04BFE575};
// What "Output_HDR_0x########" and "Output_SDR_0x########" replace
constexpr std::array<uint32_t, 8> SHADER_HASHES_OUTPUT_HDR = {0xA8EB118F, 0x922A71D1, 0x3A4D858E, 0xD950DA01, 0x5CD12E67, 0x3B489929, 0x8D04181D, 0x6846FF90};
constexpr std::array<uint32_t, 8> SHADER_HASHES_OUTPUT_SDR = {0x506D5998, 0xF68D39B5, 0xBBB9CE42, 0x51E2B894, 0x803889E8, 0xD96EF76D, 0x5C2D3A71, 0x66162229};
// Named for debugging only
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOWNSAMPLE_BLOOM_2 = {0xA77F0B56};
constexpr std::array<uint32_t, 2> SHADER_HASHES_UPSCALE_BLOOM = {0xD9E87012, 0x46727E9A};
constexpr std::array<uint32_t, 2> SHADER_HASHES_BLUR_BLOOM = {0xCCD7FA05, 0x69467442};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION = {0x1D610CBA};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_1 = {0x8BD60486};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_2 = {0xE6A8D4FB};

Luma::Math::float2 ToFloat(Luma::Math::uint2 size)
{
   return {float(size.x), float(size.y)};
}

// Called at every draw with a Luma shader (LumaData is uploaded when its bytes change)
void UpdateLumaInstanceDataCB(CB::LumaInstanceData& data, const GameDeviceData& game_device_data, const Luma::Frame::Resolutions& resolutions)
{
   const Luma::Math::float2 render_resolution = ToFloat(resolutions.render);
   data.GameData.RenderResolution = {render_resolution.x, render_resolution.y, 1.0f / render_resolution.x, 1.0f / render_resolution.y};
   data.GameData.OutputResolution = {game_device_data.upscaled_render_resolution.x, game_device_data.upscaled_render_resolution.y,
      1.0f / game_device_data.upscaled_render_resolution.x, 1.0f / game_device_data.upscaled_render_resolution.y};
   data.GameData.ResolutionScale = {game_device_data.resolution_scale, 1.0f / game_device_data.resolution_scale};
   data.GameData.DrewUpscaling = 0; // 0 until SR sets it
   data.GameData.ViewportRect = game_device_data.viewport_rect;
}

// At creation and after each resize: Core set the device's output resolution from the swapchain before this.
// Either API's device.
void OnInitSwapchain(Luma::Device device)
{
   GameDeviceData* game_device_data = Luma::Game::GetGameDeviceData(device);
   if (!game_device_data)
      return;
   // Start from here, we then update it later in case the game rendered with black bars due to forcing a different aspect ratio
   // from the swapchain buffer
   const Luma::Math::float2 output_resolution = ToFloat(Luma::Game::Frame::Get(device).output);
   game_device_data->upscaled_render_resolution = output_resolution;
   // Set where its inputs change (TAA's callback, not built yet, will too), on the presenting thread: published for the draws until
   // the next present
   cb_luma_global_settings.GameSettings.can_sharpen = output_resolution == game_device_data->upscaled_render_resolution ? 1.f : 0.f;
   Luma::Game::MarkLumaSettingsDirty();
}

// The per frame random value. TAA and SR's per frame work isn't built yet.
void OnPresent()
{
   static std::mt19937 random_generator(uint32_t(std::chrono::system_clock::now().time_since_epoch().count()));
   static auto random_range = static_cast<float>((std::mt19937::max)() - (std::mt19937::min)());
   cb_luma_global_settings.GameSettings.custom_random = static_cast<float>(random_generator() + (std::mt19937::min)()) / random_range;
}

// The game's own HDR option picks its HDR or SDR output pass, and LumaSettings' display mode follows it (Core applies it at the next
// present). Marking post processing done waits for the frame markers, not built yet.
Luma::Game::DrawResult OnDrawOutputHdr()
{
   Luma::Game::Display::SetHdr(true);
   return Luma::Game::DrawResult::None;
}

Luma::Game::DrawResult OnDrawOutputSdr()
{
   Luma::Game::Display::SetHdr(false);
   return Luma::Game::DrawResult::None;
}

// The game runs on D3D11 or D3D12 (its -d3d11 and -dx12 switches): the same code from each API's events
namespace D3D11Events {
void OnInitSwapchain(IDXGISwapChain*, ID3D11Device* device, bool)
{
   ::OnInitSwapchain(device);
}

void OnPresent(IDXGISwapChain*, ID3D11Device*)
{
   ::OnPresent();
}

Luma::Game::DrawResult OnDrawOutputHdr(const Luma::Game::D3D11::Call&)
{
   return ::OnDrawOutputHdr();
}

Luma::Game::DrawResult OnDrawOutputSdr(const Luma::Game::D3D11::Call&)
{
   return ::OnDrawOutputSdr();
}
} // namespace D3D11Events

namespace D3D12Events {
Luma::Game::DrawResult OnDrawOutputHdr(const Luma::Game::D3D12::Call&)
{
   return ::OnDrawOutputHdr();
}

Luma::Game::DrawResult OnDrawOutputSdr(const Luma::Game::D3D12::Call&)
{
   return ::OnDrawOutputSdr();
}

// The queue's device: the one the game data and resolutions are kept on
void OnInitSwapchain(IDXGISwapChain*, ID3D12CommandQueue* queue, bool)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
      ::OnInitSwapchain(device.Get());
}

void OnPresent(IDXGISwapChain*, ID3D12CommandQueue*)
{
   ::OnPresent();
}
} // namespace D3D12Events

namespace Defines = Luma::Shaders::Defines;
using Luma::Settings::EnumIndex;
using Luma::Settings::Range;
using Luma::Settings::Setting;
using Luma::Settings::Sources::Binding;

constexpr std::array<std::string_view, 2> TONEMAP_TYPES = {"Vanilla", "Neutwo"};

// The settings, with the shipped mod's keys. Sliders print "%.0f" by default.
const std::array SETTINGS = {
   Setting{
      .key = "FXBloom",
      .label = "Bloom",
      .tooltip = "Bloom strength multiplier. Default is 50.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_bloom, [](float value) { return value * 0.02f; }}, // Scale down to 0.0-2.0 for the shader
   },
   Setting{
      .key = "FXVignette",
      .label = "Vignette",
      .tooltip = "Vignette strength multiplier. Default is 50.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_vignette, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "FXFilmGrain",
      .label = "Film Grain",
      .tooltip = "Film grain strength multiplier. Default is 50, for Vanilla look set to 0.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_film_grain_strength, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "FXRCAS",
      .label = "Sharpness",
      .tooltip = "RCAS strength multiplier. Default is 50, for Vanilla look set to 0.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_sharpness_strength, [](float value) { return value * 0.01f; }},
   },
   Setting{
      .key = "FXHDRVideos",
      .label = "HDR Videos",
      .tooltip = "Enable or disable HDR video playback. Default is On.",
      .category = "Post Processing",
      .default_value = true,
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_hdr_videos},
   },
   Setting{
      .key = "TonemapType",
      .label = "Tone Map Type",
      .tooltip = "Tone mapping algorithm to use",
      .category = "Color Grading",
      .default_value = EnumIndex{1},
      .enum_labels = TONEMAP_TYPES,
      .source = Binding{&cb_luma_global_settings.GameSettings.tonemap_type},
   },
   Setting{
      .key = "CustomLUTStrength",
      .label = "LUT Strength",
      .tooltip = "LUT strength multiplier.",
      .category = "Color Grading",
      .default_value = 100.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_lut_strength, [](float value) { return value * 0.01f; }},
   },
   Setting{
      .key = "ColorGradeHueCorrection",
      .label = "Hue Shift",
      .category = "Color Grading",
      .default_value = 0.3f,
      .range = Range{0.f, 1.f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.hue_correction_strength},
   },
   Setting{
      .key = "ColorGradeExposure",
      .label = "Exposure",
      .category = "Color Grading",
      .default_value = 1.f,
      .range = Range{0.f, 2.f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.exposure},
   },
   Setting{
      .key = "ColorGradeHighlights",
      .label = "Highlights",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.highlights, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeShadows",
      .label = "Shadows",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.shadows, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeContrast",
      .label = "Contrast",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.contrast, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeSaturation",
      .label = "Saturation",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.saturation, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeHighlightSaturation",
      .label = "Highlight Saturation",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.highlight_saturation, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeBlowout",
      .label = "Blowout",
      .category = "Color Grading",
      .default_value = 20.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.blowout, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "ColorGradeFlare",
      .label = "Flare",
      .category = "Color Grading",
      .default_value = 0.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.flare, [](float value) { return value * 0.02f; }},
   },
};
} // namespace

void Luma::Game::Init()
{
   SetInfo({
      .name = "Final Fantasy VII Remake",
      .version = "2.0.0-stage1",
      .description = DESCRIPTION,
      .credits = CREDITS,
      .links = LINKS,
   });

   Shaders::SetDebugName("TAA", SHADER_HASHES_TAA);
   Shaders::SetDebugName("Title", SHADER_HASHES_TITLE);
   Shaders::SetDebugName("DoF", SHADER_HASHES_DOF);
   Shaders::SetDebugName("Motion Blur", SHADER_HASHES_MOTION_BLUR);
   // The first bloom downsample pass, at most 50% downscaling: from a 75% render scale it goes to 50%, so the bloom chain isn't
   // really affected by the render resolution
   Shaders::SetDebugName("Downsample Bloom", SHADER_HASHES_DOWNSAMPLE_BLOOM);
   // Its result (4x3 or so) is used for bloom, maybe also as a local exposure map
   Shaders::SetDebugName("Downsample Bloom", SHADER_HASHES_DOWNSAMPLE_BLOOM_2);
   // Several versions (maybe one for DRS)
   Shaders::SetDebugName("Upscale Bloom", SHADER_HASHES_UPSCALE_BLOOM);
   Shaders::SetDebugName("Blur Bloom", SHADER_HASHES_BLUR_BLOOM);
   Shaders::SetDebugName("Apply Bloom", SHADER_HASHES_BLOOM);
   Shaders::SetDebugName("Slowdown Menu", SHADER_HASHES_MENU_SLOWDOWN);
   Shaders::SetDebugName("Upscale and Tonemap", SHADER_HASHES_TONEMAP);
   Shaders::SetDebugName("Velocity Flatten", SHADER_HASHES_VELOCITY_FLATTEN);
   Shaders::SetDebugName("Velocity Gather", SHADER_HASHES_VELOCITY_GATHER);
   Shaders::SetDebugName("Ambient Occlusion", SHADER_HASHES_AMBIENT_OCCLUSION);
   Shaders::SetDebugName("Ambient Occlusion Temporal (SSAO algorithm)", SHADER_HASHES_SSAO_TEMPORAL);
   Shaders::SetDebugName("Ambient Occlusion Denoise 1", SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_1);
   Shaders::SetDebugName("Ambient Occlusion Denoise 2", SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_2);
   Shaders::SetDebugName("Output HDR", SHADER_HASHES_OUTPUT_HDR);
   Shaders::SetDebugName("Output SDR", SHADER_HASHES_OUTPUT_SDR);
   Shaders::RedirectHashes("Output_HDR", SHADER_HASHES_OUTPUT_HDR);
   Shaders::RedirectHashes("Output_SDR", SHADER_HASHES_OUTPUT_SDR);

   // Luma's defines for this game
   Shaders::SetDefine(Defines::POST_PROCESS_SPACE_TYPE, 1);
   Shaders::SetDefine(Defines::EARLY_DISPLAY_ENCODING, true);
   Shaders::SetDefine(Defines::VANILLA_ENCODING_TYPE, 1);
   Shaders::SetDefine(Defines::GAMMA_CORRECTION_TYPE, 1);
   Shaders::SetDefine(Defines::UI_DRAW_TYPE, 1);
   // The game's own shader define
   Shaders::AddDefine({
      .name = "SSAO_ALGORITHM",
      .label = "SSAO Algorithm",
      .tooltip = "AO algorithm\n0 - Original game SSAO\n1 - VBAO",
      .default_value = 1,
      .range = Range{0, 1},
   });
   Settings::Register(SETTINGS);

   SetOptions({
      .cbuffers = {
         .settings_slot = 13,
         .data_slot = 12,
         .update_luma_instance_data = &UpdateLumaInstanceDataCB,
      },
      // The FAST noise texture, which SSAO Temporal and the Dithering Fix recipe read
      .textures = {.load = true},
      .samplers = {.upgrade = true},
      .swapchain = {.mode = Luma::Swapchain::Mode::Mirror, .hdr_output = Luma::Swapchain::OutputMode::Hdr10},
      .device_data = GameDeviceDataType::Of<GameDeviceData>(),
   });
   // Texture upgrades (10 bit unorm and 11 bit float to 16 bit float). 8 bit RGBA and BGRA are probably not needed.
   Resources::SetUpgrades({
      .formats = {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R11G11B10_FLOAT},
      // Upgrade all 16:9 render targets too, because the game defaults to that aspect ratio internally unless mods are applied
      .size_filters = Luma::Resources::Upgrade::SizeFilters::SwapchainResolution | Luma::Resources::Upgrade::SizeFilters::SwapchainAspectRatio |
         Luma::Resources::Upgrade::SizeFilters::CustomAspectRatio,
      .custom_aspect_ratios = {16.f / 9.f},
      // LUT is 3D 32x
      .lut = {.size = 32, .dimensions = Luma::Resources::Upgrade::LutDimensions::_3D},
   });
   SetEvents(D3D11::Events{
      .on_init_swapchain = &D3D11Events::OnInitSwapchain,
      .on_present = &D3D11Events::OnPresent,
   });
   SetEvents(D3D12::Events{
      .on_init_swapchain = &D3D12Events::OnInitSwapchain,
      .on_present = &D3D12Events::OnPresent,
   });
   D3D11::OnShaderDraw(SHADER_HASHES_OUTPUT_HDR, &D3D11Events::OnDrawOutputHdr);
   D3D11::OnShaderDraw(SHADER_HASHES_OUTPUT_SDR, &D3D11Events::OnDrawOutputSdr);
   D3D12::OnShaderDraw(SHADER_HASHES_OUTPUT_HDR, &D3D12Events::OnDrawOutputHdr);
   D3D12::OnShaderDraw(SHADER_HASHES_OUTPUT_SDR, &D3D12Events::OnDrawOutputSdr);
   // Not built yet: the TAA callback (then SR), the output pass's TAA gate and post processing marker, DoF and Motion Blur (they need
   // the SR sampler). SSAO Temporal's FAST noise at t6 needs no callback: the replacement reads "fast_noise" by name, which Core binds
   // on both APIs.
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   return Luma::DllMain(module, reason, reserved);
}
