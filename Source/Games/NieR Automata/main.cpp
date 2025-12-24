#define GAME_NIER_AUTOMATA 1

#include "..\..\Core\core.hpp"

class NierAutomata final : public Game
{
public:
   void OnInit(bool async) override
   {
      // The game used 12 and 13, and all other buffers than 6.
      luma_settings_cbuffer_index = 6;
#if DEVELOPMENT // We only ever need this in development to debug textures, the game doesn't have any unused cbuffers left, and I'm not 100% sure they'd get re-set between draw calls that might override it (we don't have a way to restore it after a draw call yet)
      luma_data_cbuffer_index = 10;
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("NieR:Automata Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "NieR:Automata Luma mod");
      Globals::VERSION = 1;
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
#if 0 // These aren't really needed and cause glitches, the game is all R8G8B8A8_UNORM and R11G11B10_FLOAT until it copies on the R10G10B10A2_UNORM swapchain (at least in HDR!)
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
#endif

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      game = new NierAutomata();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}