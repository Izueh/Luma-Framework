#define GAME_BURNOUT_PARADISE 1

#include "..\..\Core\core.hpp"

namespace
{
   bool hdr_car_reflections = true;
}

class BurnoutParadise final : public Game
{
public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_IMPROVED_MOTION_BLUR", '1', true, false, "Increase the quality of the game's motion blur in multiple ways", 1},
         {"ENABLE_IMPROVED_BLOOM", '1', true, false, "Increase the quality of the game's motion blur, and makes it more \"HDR\"", 1},
         {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the game's vignette. This will also disable the blue filter and increase the brightness of the whole image", 1},
         {"REMOVE_BLACK_BARS", '0', true, false, "Removes ugly black bars from Ultrawide, given that often menus and game were both pillarboxed and letterboxed at the same time.\nThis will also reveal some bad menus backgrounds", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning to the end.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');

      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      reshade::get_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(runtime, NAME, "MotionBlurIntensity", cb_luma_global_settings.GameSettings.MotionBlurIntensity);
      reshade::get_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      reshade::get_config_value(runtime, NAME, "ColorGradingDebluingIntensity", cb_luma_global_settings.GameSettings.ColorGradingDebluingIntensity);
      reshade::get_config_value(runtime, NAME, "HDRBoostIntensity", cb_luma_global_settings.GameSettings.HDRBoostIntensity);
      reshade::get_config_value(runtime, NAME, "OriginalTonemapperColorIntensity", cb_luma_global_settings.GameSettings.OriginalTonemapperColorIntensity);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point

      reshade::get_config_value(runtime, NAME, "HDRCarReflections", hdr_car_reflections); // Allow disabling this for performance reasons
      if (hdr_car_reflections)
      {
         // Note: this is very much overkill but it will make car reflections cubemaps HDR too, given they had some clipping. This can help us make the sky brighter with an HDR boost too!
         texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::Cubes;
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::SliderFloat("Bloom Intensity", &cb_luma_global_settings.GameSettings.BloomIntensity, 0.f, 2.f))
      {
         reshade::set_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomIntensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity", runtime);

      if (ImGui::SliderFloat("Motion Blur Intensity", &cb_luma_global_settings.GameSettings.MotionBlurIntensity, 0.f, 2.f))
      {
         reshade::set_config_value(runtime, NAME, "MotionBlurIntensity", cb_luma_global_settings.GameSettings.MotionBlurIntensity);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.MotionBlurIntensity, default_luma_global_game_settings.MotionBlurIntensity, "MotionBlurIntensity", runtime);

      if (ImGui::SliderFloat("Color Grading Intensity", &cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.ColorGradingIntensity, default_luma_global_game_settings.ColorGradingIntensity, "ColorGradingIntensity", runtime);

      if (ImGui::SliderFloat("Color Grading De-Bluing Intensity", &cb_luma_global_settings.GameSettings.ColorGradingDebluingIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "ColorGradingDebluingIntensity", cb_luma_global_settings.GameSettings.ColorGradingDebluingIntensity);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("The game's default color grading made the whole game blue, this attempts to restore a more neutral color, without removing adjustments to contrast or saturation.");
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.ColorGradingDebluingIntensity, default_luma_global_game_settings.ColorGradingDebluingIntensity, "ColorGradingDebluingIntensity", runtime);

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::SliderFloat("HDR Boost Intensity", &cb_luma_global_settings.GameSettings.HDRBoostIntensity, 0.f, 2.f))
         {
            reshade::set_config_value(runtime, NAME, "HDRBoostIntensity", cb_luma_global_settings.GameSettings.HDRBoostIntensity);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Enable a \"Fake\" HDR boosting effect. Set to 0 for the vanilla look.");
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.HDRBoostIntensity, default_luma_global_game_settings.HDRBoostIntensity, "HDRBoostIntensity", runtime);
      }

      if (ImGui::SliderFloat("Original Tonemapper Color Intensity", &cb_luma_global_settings.GameSettings.OriginalTonemapperColorIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "OriginalTonemapperColorIntensity", cb_luma_global_settings.GameSettings.OriginalTonemapperColorIntensity);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Move closer to 1 to restore a look closer to the original (more desaturated).");
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.OriginalTonemapperColorIntensity, default_luma_global_game_settings.OriginalTonemapperColorIntensity, "OriginalTonemapperColorIntensity", runtime);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Template Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Burnout Paradise Remastered - Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      // The game has issues with motion blur warping when using direct upgrades
      enable_indirect_texture_format_upgrades = true;
      enable_automatic_indirect_texture_format_upgrades = true;

      // Game floors are blurry without this
      enable_samplers_upgrade = true;

      // With or without these the game occasionally loses input and draws behind the Windows taskbar, even when "force_borderless" is true, so let's just keep it vanilla, it already offers FSE/borderless/windowed
      prevent_fullscreen_state = false;
#if 0
      force_borderless = true;
#endif

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("F382FC35", nullptr, 16), "Downscale Depth");
      forced_shader_names.emplace(std::stoul("E1564B55", nullptr, 16), "Downscale Depth"); // MSAA version
      forced_shader_names.emplace(std::stoul("60F03AA9", nullptr, 16), "Linearize Depth");
      forced_shader_names.emplace(std::stoul("808BC446", nullptr, 16), "Gen Bloom");
      forced_shader_names.emplace(std::stoul("0325730A", nullptr, 16), "Blur Bloom");
      forced_shader_names.emplace(std::stoul("C7835AB9", nullptr, 16), "Gen DoF");
      forced_shader_names.emplace(std::stoul("01FF871A", nullptr, 16), "Gen SSAO");
      forced_shader_names.emplace(std::stoul("EA125DFC", nullptr, 16), "Blur DoF or SSAO");
      forced_shader_names.emplace(std::stoul("F4CB0620", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("9943A357", nullptr, 16), "Clear Particles Color and Copy Depth");
      forced_shader_names.emplace(std::stoul("923A9E10", nullptr, 16), "Asphalt Lines");
#endif

      redirected_shader_hashes["Tonemap"] =
      {
         "5A34E415",
         "6B63F6E9",
         "7E095B44",
         "36F104E3",
         "57B58AF1",
         "259C7CD1",
         "382FAE1E",
         "959BB01D",
         "4933D9DA",
         "9613B478",
         "07297021",
         "10807557",
         "A4AAD10C",
         "A47D890F",
         "B9F09845",
         "BE5A4C0C",
         "BF6A19BE",
         "C0BD8148",
         "C0305DC5",
         "D29A0825",
         "D48AAAA6",
         "D772072E",
         "DD905507",
         "FDE602F4",
      };

      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.MotionBlurIntensity = 1.f;
      default_luma_global_game_settings.ColorGradingIntensity = 1.f;
      default_luma_global_game_settings.ColorGradingDebluingIntensity = 0.f;
      default_luma_global_game_settings.HDRBoostIntensity = 1.f;
      default_luma_global_game_settings.OriginalTonemapperColorIntensity = 0.f;
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

      game = new BurnoutParadise();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}