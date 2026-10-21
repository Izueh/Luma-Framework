#define GAME_NIER_REPLICANT 1

#define ALLOW_SHADERS_DUMPING 0

#include "..\..\Core\core.hpp"

namespace
{
   bool first_boot = true; // Automatic setting
   bool enable_hdr = false;
   bool next_enable_hdr = enable_hdr; // The value we serialize, that will be ignored until reboot
   float2 projection_jitters = {0, 0};
   ShaderHashesList shader_hashes_light;
} // namespace

struct GameDeviceDataNierReplicant final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   std::atomic<bool> has_drawn_upscaling = false;
#endif // ENABLE_SR
};

class NierReplicant final : public Game
{
   static GameDeviceDataNierReplicant& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataNierReplicant*>(device_data.game);
   }

#if ENABLE_SR
   static bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }
#endif

public:
   void OnInit(bool async) override
   {
       GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
       GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
       GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
       GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      if (!enable_hdr)
      {
         cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;
         cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
         cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
         cb_luma_global_settings.UIPaperWhite = srgb_white_level;
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      return DrawOrDispatchOverrideType::None;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataNierReplicant;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
#if ENABLE_SR
      device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
      game_device_data.has_drawn_upscaling = false;

      // Update TAA jitters:
      int phases = SR::GetDefaultJitterPhases();
      if (device_data.sr_type != SR::Type::None)
      {
         auto* sr_instance_data = device_data.GetSRInstanceData();
         phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
      }
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;
      projection_jitters.x = SR::HaltonSequence(temporal_frame, 2);
      projection_jitters.y = SR::HaltonSequence(temporal_frame, 3);

       if (!custom_texture_mip_lod_bias_offset)
      {
          std::shared_lock shared_lock_samplers(s_mutex_samplers);
          if (SrActive(device_data) &&
              game_device_data.render_resolution.y > 0.0f &&
              game_device_data.upscale_resolution.y > 0.0f)
          {
             device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(game_device_data.render_resolution.y, game_device_data.upscale_resolution.y); // This results in -1 at output res
          }
          else
          {
             device_data.texture_mip_lod_bias_offset = 0.f;
          }
       }
#endif
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Enable HDR", &next_enable_hdr))
      {
         reshade::set_config_value(runtime, NAME, "EnableHDR", next_enable_hdr);
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("NieR Replicant™ ver.1.22474487139... Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "NieR Replicant™ ver.1.22474487139... Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      enable_samplers_upgrade = true;

      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm};

      luma_settings_cbuffer_index = 7;
      swapchain_upgrade_type = SwapchainUpgradeType::None;

      game = new NierReplicant();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      reshade::get_config_value(nullptr, NAME, "FirstBoot", first_boot);
      if (first_boot)
      {
         reshade::set_config_value(nullptr, NAME, "FirstBoot", false);

         // Automatically enable HDR in the mod if it's supported on the primary display on first boot
         bool hdr_supported_display;
         bool hdr_enabled_display;
         Display::IsHDRSupportedAndEnabled(0, hdr_supported_display, hdr_enabled_display);
         enable_hdr = hdr_supported_display;

         reshade::set_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      else
      {
         reshade::get_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      next_enable_hdr = enable_hdr;

      if (enable_hdr)
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      }
      else
      {
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         force_disable_display_composition = true;
      }
   }

   return TRUE;
}