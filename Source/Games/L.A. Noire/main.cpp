#define GAME_LA_NOIRE 1

#include "..\..\Core\core.hpp"

struct GameDeviceDataTemplate final : public GameDeviceData
{
};

class GameShadowOfMordor final : public Game
{
   static GameDeviceDataTemplate& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataTemplate*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      luma_settings_cbuffer_index = 10;
      luma_data_cbuffer_index = 9;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataTemplate;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, ShaderStages stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None;
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Template Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "L.A. Noire Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::NonFunctional;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            //DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            //DXGI_FORMAT_R8G8B8A8_TYPELESS,
            //DXGI_FORMAT_B8G8R8A8_UNORM, // Lego
            //DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            //DXGI_FORMAT_B8G8R8A8_TYPELESS,
            //DXGI_FORMAT_B8G8R8X8_UNORM,
            //DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
            //DXGI_FORMAT_B8G8R8X8_TYPELESS,
            //DXGI_FORMAT_R11G11B10_FLOAT,
      };

      enable_samplers_upgrade = true;

      SetGame<GameShadowOfMordor>();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}