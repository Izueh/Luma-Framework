#define GAME_MIRRORS_EDGE_CATALYST 1

// Any message box breaks the input of the game forever
#define DISABLE_AUTO_DEBUGGER 1

#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define AVOID_INPUT_LOSS 1

#include "..\..\Core\core.hpp"

class MirrorsEdgeCatalyst final : public Game // ### Rename this to your game's name ###
{
public:
   void OnInit(bool async) override
   {
      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Mirror's Edge Catalyst Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mirror's Edge Catalyst - Luma mod"); // ### Rename this ###
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      enable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
         DXGI_FORMAT_R8G8B8A8_UNORM,
         // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
         // DXGI_FORMAT_R8G8B8A8_TYPELESS,
         // DXGI_FORMAT_B8G8R8A8_UNORM,
         // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
         // DXGI_FORMAT_B8G8R8A8_TYPELESS,
         // DXGI_FORMAT_B8G8R8X8_UNORM,
         // DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
         // DXGI_FORMAT_B8G8R8X8_TYPELESS,

         DXGI_FORMAT_R11G11B10_FLOAT,
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      SetGame<MirrorsEdgeCatalyst>();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}