// ### Rename this ###
#define GAME_TEMPLATE 1

#include "..\..\Core\core.hpp"

class GameShadowOfMordor final : public Game // ### Rename this to your game's name ###
{
public:
   void OnInit(bool async) override
   {
      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // Game is all linear, sRGB swapchain (made for gamma 2.2 as usual)
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
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
      Globals::SetGlobals(PROJECT_NAME, "Template Luma mod"); // ### Rename this ###
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::NonFunctional;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::None; // Crashes on boot
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

#if 0
            DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R10G10B10A2_TYPELESS,

            DXGI_FORMAT_R16G16B16A16_UNORM,

            DXGI_FORMAT_R11G11B10_FLOAT,
#endif
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      SetGame<GameShadowOfMordor>();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}