#define GAME_VANQUISH 1

#include "..\..\Core\core.hpp"

class Vanquish final : public Game
{
public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"IMPROVED_COLOR_GRADING_TYPE", '1', true, false, "Improves the post process color grading in multiple ways", 2},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Enables post process color grading", 1},
         {"ENABLE_HDR_BOOST", '1', true, false, "The game didn't have much dynamic range; this expands it", 1},
         {"ENABLE_VANILLA_HIGHLIGHTS_EMULATION", '0', true, false, "Desaturates highlights like they would have in SDR, given they all clipped to white and can end up being too colorful in HDR", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      // The game is all SDR (UNORM) gamma space, even when the "HDR" toggle is enable in the graphics settings (there's a few _SRGB views in it, but only temporarily)
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      // NOTE: for now the UI doesn't have a separate brightness scale because it's widely connected to gameplay and applies some filters/helmet effects to it
      
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }
   
   // TODO: specify that it needs voodoo, and also that the "HDR" setting in the game isn't related to Luma (and seemingly does nothing)
   // If we ever convert the mod to native DX9, it'd be enough to:
   // - Match the DX11 converted shaders with the DX9 original
   // - The "GBufferComposition..." and "BloomVolume" shaders simply need sRGB conversions in and out, given we upgraded textures from R8G8B8A8_UNORM to R16G16B16A16_FLOAT, and they don't support _SRGB views. There's no saturate() to be removed if not in the tonemap shader later.
   //   There's 3 ways to approach that:
   //   - Manually edit the shaders to do sRGB to linear and linear to sRGB conversions
   //   - Automatically patch the shaders to do sRGB to linear and linear to sRGB conversions (by adding instructions with live patching directly in memory, with pattern recognition etc)
   //   - Create a copy of a texture with sRGB conversion applied every time it's written and read again by a mismatching sRGB view
   //   - Do not upgrade the gbuffer textures, only upgrade the final buffer that composes them, by shader hash (this won't work great, might cause banding)
   // Extras to improve the mod:
   // - Possibly fix a few more of the "GBufferComposition..." and "BloomVolume", and add saturate() on the output of particles
   // - Add AutoHDR to videos
   // - Restore some hue shifting after TM?
   // - Add a frame limiter to handle 30 or 60 fps fixed
   // - Scale up bloom mip size (it looks terrible)
   // - Check motion blur (especially in UW)
   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Vanquish\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Vanquish Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true; // Might not be needed but works with it!
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
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

            reshade::api::format::r16g16b16a16_unorm,

            // I don't think DX9 even had the following formats:
            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

      // Bloom mips is always done at this resolution: 320x176.
      // Also some textures are resized to the target resolution before the swapchain so hardcode the native display resolution in the list, to make sure they are upgraded anyway.
      // Also force upgrade 16:9 because unless aspect ratio is locked, the game runs within 16:9.
      int screen_width = GetSystemMetrics(SM_CXSCREEN);
      int screen_height = GetSystemMetrics(SM_CYSCREEN);
      texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = { 320.f / 176.f, float(screen_width) / float(screen_height), 16.f / 9.f };

      game = new Vanquish();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}