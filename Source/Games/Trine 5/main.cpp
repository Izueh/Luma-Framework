#define GAME_TRINE_5 1

#define UPGRADE_SAMPLERS 0

#define LUMA_GAME_SETTING_01 float HDRHighlights
#define LUMA_GAME_SETTING_02 float HDRDesaturation

#include "..\..\Core\core.hpp"

// Dev only as skipping sharpening also skipps TAA
#define SKIP_SHARPEN_TYPE 0

namespace
{
   constexpr float default_hdr_highlights = 0.3f;
   constexpr float default_hdr_desaturation = 0.8666f;

   ShaderHashesList pixel_shader_hashes_SharpenPreparation;
   ShaderHashesList pixel_shader_hashes_Sharpen;
   ShaderHashesList pixel_shader_hashes_TAA;
   ShaderHashesList pixel_shader_hashes_Tonemap;
}

struct GameDeviceDataTrine5 final : public GameDeviceData
{
   com_ptr<ID3D11Texture2D> upgraded_post_process_texture;
   com_ptr<ID3D11ShaderResourceView> upgraded_post_process_srv;
   com_ptr<ID3D11RenderTargetView> upgraded_post_process_rtv;
   bool drew_tonemap = false;
   bool drew_sharpen = false;
#if DEVELOPMENT
   bool skip_sharpen = false;
#endif
};

class Trine5 final : public Game
{
   static GameDeviceDataTrine5& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataTrine5*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', false, false, "0 - SDR: Vanilla\n1 - HDR: Vanilla+ (native method)\n2 - HDR: Vanilla+ (inverse method)\n3 - HDR: Untonemapped"},
         {"ENABLE_VIGNETTE", '1', false, false, "Set to 0 to disable vanilla vignette"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "HDRHighlights", cb_luma_frame_settings.HDRHighlights);
      reshade::get_config_value(runtime, NAME, "HDRDesaturation", cb_luma_frame_settings.HDRDesaturation);
   }

   void DrawImGuiSettings(DeviceData& device_data)
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::TreeNode("Advanced Settings"))
		{
         if (ImGui::SliderFloat("HDR Highlights", &cb_luma_frame_settings.HDRHighlights, 0.f, 1.f))
         {
            device_data.cb_luma_frame_settings_dirty = true;
         }
         ImGui::SameLine();
         if (cb_luma_frame_settings.HDRHighlights != default_hdr_highlights)
         {
            ImGui::PushID("HDR Highlights");
            if (ImGui::SmallButton(ICON_FK_UNDO))
            {
               cb_luma_frame_settings.HDRHighlights = default_hdr_highlights;
               reshade::set_config_value(runtime, NAME, "HDRHighlights", cb_luma_frame_settings.HDRHighlights);
            }
            ImGui::PopID();
         }
         else
         {
            const auto& style = ImGui::GetStyle();
            ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
            size.x += style.FramePadding.x;
            size.y += style.FramePadding.y;
            ImGui::InvisibleButton("", ImVec2(size.x, size.y));
         }

         if (ImGui::SliderFloat("HDR Desaturation", &cb_luma_frame_settings.HDRDesaturation, 0.f, 1.f))
         {
            device_data.cb_luma_frame_settings_dirty = true;
         }
         ImGui::SameLine();
         if (cb_luma_frame_settings.HDRDesaturation != default_hdr_desaturation)
         {
            ImGui::PushID("HDR Desaturation");
            if (ImGui::SmallButton(ICON_FK_UNDO))
            {
               cb_luma_frame_settings.HDRDesaturation = default_hdr_desaturation;
               reshade::set_config_value(runtime, NAME, "HDRDesaturation", cb_luma_frame_settings.HDRDesaturation);
            }
            ImGui::PopID();
         }
         else
         {
            const auto& style = ImGui::GetStyle();
            ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
            size.x += style.FramePadding.x;
            size.y += style.FramePadding.y;
            ImGui::InvisibleButton("", ImVec2(size.x, size.y));
         }

         ImGui::TreePop();
		}
   }
#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (ImGui::Checkbox("Skip Sharpening", &game_device_data.skip_sharpen))
      {
      }
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Trine 5\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
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
         system("start https://github.com/Filoppi/Luma");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
         "\n\nMain:"
         "\nPumbo"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         , "");
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataTrine5;
   }

   bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      auto game_device_data_prev = game_device_data;

      // We always expect sharpening immediately after TM, if there's any other shader in between, send an assert
      if (game_device_data_prev.drew_tonemap && !game_device_data_prev.drew_sharpen)
      {
			// Only one shader should be drawn between TM and sharpening, and that is the pre sharpening luma calculations shader
         ASSERT_ONCE(original_shader_hashes.Contains(pixel_shader_hashes_Sharpen) || original_shader_hashes.Contains(pixel_shader_hashes_SharpenPreparation));
      }
      // TODO: make sure all shaders that run after sharpening are UI

      // Tonemapper
      if (!game_device_data.drew_tonemap && original_shader_hashes.Contains(pixel_shader_hashes_Tonemap))
      {
         game_device_data.drew_tonemap = true;

         // If we upgrade all R10G10B10A2 textures, there's no need to do this live texture format swap
         if (enable_swapchain_upgrade && swapchain_upgrade_type == 1 && (!enable_texture_format_upgrades || !texture_upgrade_formats.contains(reshade::api::format::r10g10b10a2_unorm)))
         {
            // We manually upgrade the R10G10B10A2 texture that is used as tonemapper output and sharpening input (after which the game uses the swapchain as RT).
            // If we upgrade all R10G10B10A2 the game can crash.
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            ASSERT_ONCE(rtv);
            if (rtv && is_custom_pass)
            {
               com_ptr<ID3D11Resource> target_resource;
               rtv->GetResource(&target_resource);
               if (!AreResourcesEqual(game_device_data_prev.upgraded_post_process_texture.get(), target_resource.get(), false))
               {
                  game_device_data.upgraded_post_process_texture = nullptr;
                  game_device_data.upgraded_post_process_srv = nullptr;
                  game_device_data.upgraded_post_process_rtv = nullptr;

                  game_device_data.upgraded_post_process_texture = CloneTexture2D(native_device, target_resource.get(), DXGI_FORMAT_R16G16B16A16_FLOAT, false, false, nullptr);
                  ASSERT_ONCE(game_device_data.upgraded_post_process_texture);
                  native_device->CreateShaderResourceView(game_device_data.upgraded_post_process_texture.get(), nullptr, &game_device_data.upgraded_post_process_srv);
                  native_device->CreateRenderTargetView(game_device_data.upgraded_post_process_texture.get(), nullptr, &game_device_data.upgraded_post_process_rtv);
               }
            }
         }
         // Restoring the state isn't necessary in this game
         if (game_device_data.upgraded_post_process_rtv)
         {
            ID3D11RenderTargetView* const upgraded_post_process_rtv_const = game_device_data.upgraded_post_process_rtv.get();
            native_device_context->OMSetRenderTargets(1, &upgraded_post_process_rtv_const, nullptr);
         }

#if DEVELOPMENT && SKIP_SHARPEN_TYPE == 0
         if (game_device_data.skip_sharpen)
         {
            ASSERT_ONCE(!device_data.swapchains.empty());
            reshade::api::swapchain* swapchain = *device_data.swapchains.begin();
            IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
            SwapchainData& swapchain_data = *swapchain->get_private_data<SwapchainData>();
            UINT back_buffer_index = swapchain->get_current_back_buffer_index();
            com_ptr<ID3D11Texture2D> back_buffer;
            native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));

            // Ignore sharpening for now, write to the swapchain directly
            ID3D11RenderTargetView* const display_composition_rtv_const = swapchain_data.display_composition_rtvs[back_buffer_index].get();
            native_device_context->OMSetRenderTargets(1, &display_composition_rtv_const, nullptr);
         }
#endif
      }

      if (game_device_data.drew_tonemap && original_shader_hashes.Contains(pixel_shader_hashes_SharpenPreparation))
      {
#if DEVELOPMENT
         if (game_device_data.skip_sharpen)
         {
            return true;
         }
#endif
			// TODO: if we don't skip this, we need to fix it to work with HDR linear colors (but we could just plug in our own AMD CAS implementation anyway)
      }

#if !DEVELOPMENT
      // TODO: maybe we could just rely on pixel_shader_hashes_SharpenPreparation?
      if (original_shader_hashes.Contains(pixel_shader_hashes_TAA))
      {
         static bool warning_sent = false;
			if (!warning_sent)
			{
				warning_sent = true;
            MessageBoxA(game_window, "Luma detected that TAA was enabled in the game graphics settings.\nPlease select a form of Anti Aliasing that is not TAA or DLSS for compatibility with Luma.", NAME, MB_SETFOREGROUND);
			}
      }
#endif

      // Sharpening
      if (game_device_data.drew_tonemap && !game_device_data.drew_sharpen && original_shader_hashes.Contains(pixel_shader_hashes_Sharpen))
      {
         game_device_data.drew_sharpen = true; // Set to true even if we skip it

#if DEVELOPMENT
         if (game_device_data.skip_sharpen)
         {
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->CSGetShaderResources(0, 1, &srv);
            com_ptr<ID3D11UnorderedAccessView> uav;
            native_device_context->CSGetUnorderedAccessViews(0, 1, &uav);
            com_ptr<ID3D11Resource> source_resource;
            if (srv)
            {
               srv->GetResource(&source_resource);
            }
            com_ptr<ID3D11Resource> target_resource;
            if (uav)
            {
               uav->GetResource(&target_resource);
            }
            if (target_resource && source_resource)
            {
#if SKIP_SHARPEN_TYPE >= 1
               native_device_context->CopyResource(target_resource.get(), game_device_data.upgraded_post_process_texture.get() ? game_device_data.upgraded_post_process_texture.get() : source_resource.get());
#endif
               return true;
            }
            else
            {
					ASSERT_ONCE(false); // Failed to skip sharpening
            }
         }
         else
#endif
         if (game_device_data.upgraded_post_process_srv.get())
         {
            ID3D11ShaderResourceView* const upgraded_post_process_srv_const = game_device_data.upgraded_post_process_srv.get();
            native_device_context->CSSetShaderResources(0, 1, &upgraded_post_process_srv_const);
         }
      }

#if DEVELOPMENT
      // Check what was the previous shader
      static ShaderHashesList original_shader_hashes2;
		original_shader_hashes2 = original_shader_hashes;
#endif

      return false;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      ASSERT_ONCE(game_device_data.drew_tonemap == game_device_data.drew_sharpen);
      game_device_data.drew_tonemap = false;
      game_device_data.drew_sharpen = false;
      if (device_data.cloned_pipeline_count == 0)
      {
         game_device_data.upgraded_post_process_texture = nullptr;
         game_device_data.upgraded_post_process_srv = nullptr;
         game_device_data.upgraded_post_process_rtv = nullptr;
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::GAME_NAME = PROJECT_NAME;
      Globals::DESCRIPTION = "Trine 5 Luma mod";
      Globals::WEBSITE = "";
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // Upgrading any types of textures outside of the swapchain causes the game to crash when changing TAA settings or on boot
      // For now upgrading R8G8B8A8 textures is disabled because the game creates typeless ones which are then used as RTV or SRV of different types (e.g. unsigned/signed int, etc), so we'd need to add handling it.
      // Upgrading R8G8B8A8 allows the game's unfinished DLSS implementation to work, but it still has multiple problems, because DLSS runs in sRGB (linear) color space, thus it clips all colors beyond sRGB.
      // In the vanilla game DLSS was hidden, possibly because it was untested and also it had wrong gamma, given it was run instead of the TAA/sharpening pass which took gamma 2.0 and spit out gamma sRGB,
      // DLSS wouldn't have done that gamma correction (it misses it, so it outputs gamma 2.0). It also probably misses the "HDR" flag and thus would interpret colors as sRGB gamma space, which is not what LUMA does.
      // That said, if DLSS ever was upgraded to support negative scRGB colors without clipping them, we could use "DLSSTweaks" to force the HDR flag on and run it in HDR (we could even force DLAA).
      enable_swapchain_upgrade = true;
      swapchain_upgrade_type = 1;

      cb_luma_frame_settings.HDRHighlights = default_hdr_highlights;
      cb_luma_frame_settings.HDRDesaturation = default_hdr_desaturation;

      pixel_shader_hashes_SharpenPreparation.compute_shaders = { Shader::Hash_StrToNum("F5503D2E") };
		pixel_shader_hashes_Sharpen.compute_shaders = { Shader::Hash_StrToNum("78D8400E"), Shader::Hash_StrToNum("C0EF3F88"), Shader::Hash_StrToNum("AA97F987"), Shader::Hash_StrToNum("0910AE0F") }; // The last one is for DLSS (which doesn't do sharpening), the others are for sharpening and some for TAA too
      pixel_shader_hashes_TAA.compute_shaders = { Shader::Hash_StrToNum("78D8400E"), Shader::Hash_StrToNum("C0EF3F88"), Shader::Hash_StrToNum("0910AE0F") };
      pixel_shader_hashes_Tonemap.pixel_shaders = { Shader::Hash_StrToNum("2B825C00"), Shader::Hash_StrToNum("480558AD"), Shader::Hash_StrToNum("AEDB562C") };

      game = new Trine5();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}