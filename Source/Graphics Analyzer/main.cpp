#define GRAPHICS_ANALYZER 1

// Always true in the graphics analyzer
#ifdef DEVELOPMENT
#undef DEVELOPMENT
#define DEVELOPMENT 1
#endif // DEVELOPMENT

#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#include "..\Core\core.hpp"

struct GraphicsAnalyzerDeviceData final : public GameDeviceData
{
};

// Graphics analyzer project.
// This is not meant to be a separate devkit for mods, but an alternative tool that uses the same features that Luma mods have to analyze graphics in games,
// so if you want to make a new game mod, just make a new project for it, after potentially testing the game out with this (it's not necessary, but this is less invasive than a whole mod).
class GraphicsAnalyzer final : public Game
{
   static GraphicsAnalyzerDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GraphicsAnalyzerDeviceData*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      // Needed by the final display composition shader
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GraphicsAnalyzerDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("DX11 Graphics Analyzer - Developed by Pumbo", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Luma DX11 Graphics Analyzer", "https://github.com/Filoppi/Luma-Framework/");

      // Don't automatically dump game shaders
      auto_dump = false;
      // Don't automatically load custom shaders
      auto_load = false;

      // For now these are needed, because the scRGB HDR swapchain final draw shader acts as a debug draw texture visualizer.
      // TODO: avoid this. Make it work in SDR, and also just don't upgrade the swapchain!
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

      // It might break some games, but at least one can alt tab quickly.
      prevent_fullscreen_state = false;

      game = new GraphicsAnalyzer();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}