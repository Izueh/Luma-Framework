#define GAME_SHENMUE_I_AND_II 1

#include "..\..\Core\core.hpp"

class ShenmueIAndII final : public Game
{
public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // The game is all SDR (UNORM) gamma space, though it applied gamma 2.2 and then stored post processing and UI as UNORM_SRGB, using it as linear.
      // The display would have linearized it with gamma 2.2, and the UI was also drawing in linear, so using FLOAT instead of UNORM_SRGB is totally fine.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Shenmue I & II Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Shenmue I & II Luma mod");
      Globals::VERSION = 1;
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_TYPELESS,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_TYPELESS,
            DXGI_FORMAT_B8G8R8X8_UNORM,
            DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8X8_TYPELESS,
            
            DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R10G10B10A2_TYPELESS,

            DXGI_FORMAT_R11G11B10_FLOAT,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      enable_indirect_texture_format_upgrades = true; // Needed for photo mode captures // TODO: disabling this at runtime (through the dev ignore button) makes the game crash
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      SetGame<ShenmueIAndII>();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}