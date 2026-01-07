#define GAME_FINALFANTASYXV 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ALLOW_SHADERS_DUMPING 0
#define ENABLE_NGX 1

#include "..\..\Core\core.hpp"

namespace
{
    float2 projection_jitters = { 0, 0 };
	//const uint32_t shader_hash_mvec_pixel = std::stoul("FFFFFFF3", nullptr, 16);
    ShaderHashesList shader_hashes_tonemap;
    ShaderHashesList shader_hashes_TAA;
}

struct GameDeviceDataFFXV final : public GameDeviceData
{
#if ENABLE_SR
    // SR
    com_ptr<ID3D11Resource> sr_motion_vectors;
    com_ptr<ID3D11Resource> sr_source_color;
    com_ptr<ID3D11Resource> depth_buffer;
    com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
#endif // ENABLE_SR
    std::atomic<bool> has_drawn_upscaling = false;
    //com_ptr<ID3D11PixelShader> motion_vectors_ps;
};

class FinalFantasyXV final : public Game // ### Rename this to your game's name ###
{
    static GameDeviceDataFFXV& GetGameDeviceData(DeviceData& device_data)
    {
        return *static_cast<GameDeviceDataFFXV*>(device_data.game);
    }
public:
   void OnInit(bool async) override
   {
       GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
       GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
       GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
       GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
       GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // #w## Update this (find the right value) ###
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
	   auto& game_device_data = GetGameDeviceData(device_data);

	   const bool had_drawn_upscaling = game_device_data.has_drawn_upscaling;
	   if (!game_device_data.has_drawn_upscaling && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && original_shader_hashes.Contains(shader_hashes_TAA))
	   {
		   game_device_data.has_drawn_upscaling = true;
		   // 1 depth [3]
		   // 2 current color source () = [0]
		   // 3 previous color source (previous frame) = [1]
		   // 4 motion vectors [6]
		   com_ptr<ID3D11ShaderResourceView> ps_shader_resources[16];
		   native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

		   com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		   com_ptr<ID3D11DepthStencilView> depth_stencil_view;
		   native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
		   const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && ps_shader_resources[5].get() != nullptr && ps_shader_resources[6].get() != nullptr && render_target_views[0] != nullptr;
		   ASSERT_ONCE(dlss_inputs_valid);

           if (dlss_inputs_valid)
           {
              auto* sr_instance_data = device_data.GetSRInstanceData();
              ASSERT_ONCE(sr_instance_data);

               com_ptr<ID3D11Resource> output_colorTemp;
               render_target_views[0]->GetResource(&output_colorTemp);
               com_ptr<ID3D11Texture2D> output_color;
               HRESULT hr = output_colorTemp->QueryInterface(&output_color);
               ASSERT_ONCE(SUCCEEDED(hr));
               D3D11_TEXTURE2D_DESC output_texture_desc;
               output_color->GetDesc(&output_texture_desc);

               //ASSERT_ONCE(std::lrintf(device_data.output_resolution.x) == output_texture_desc.Width && std::lrintf(device_data.output_resolution.y) == output_texture_desc.Height);
               std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)device_data.sr_render_resolution_scale, (double)output_texture_desc.Height * (double)device_data.sr_render_resolution_scale, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
               bool dlss_hdr = true;

               SR::SettingsData settings_data;
               settings_data.output_width = output_texture_desc.Width;
               settings_data.output_height = output_texture_desc.Height;
               settings_data.render_width = dlss_render_resolution[0];
               settings_data.render_height = dlss_render_resolution[1];
               settings_data.dynamic_resolution = false; //TODO: figure out dsr later
               settings_data.hdr = dlss_hdr;
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

               bool skip_dlss = output_texture_desc.Width < sr_instance_data->min_resolution || output_texture_desc.Height < sr_instance_data->min_resolution;
               bool dlss_output_changed = false;
               constexpr bool dlss_use_native_uav = true;

               //reshade::log::message(reshade::log::level::info, ("DLSS initialization successful"));

               bool dlss_output_supports_uav = dlss_use_native_uav && (output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
               if (!dlss_output_supports_uav) {

                   output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                   if (device_data.sr_output_color.get())
                   {
                       D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
                       device_data.sr_output_color->GetDesc(&dlss_output_texture_desc);
                       dlss_output_changed = dlss_output_texture_desc.Width != output_texture_desc.Width || dlss_output_texture_desc.Height != output_texture_desc.Height || dlss_output_texture_desc.Format != output_texture_desc.Format;
                   }
                   if (!device_data.sr_output_color.get() || dlss_output_changed)
                   {
                       device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                       hr = native_device->CreateTexture2D(&output_texture_desc, nullptr, &device_data.sr_output_color);
                       ASSERT_ONCE(SUCCEEDED(hr));
                   }
                   if (!device_data.sr_output_color.get())
                   {
                       skip_dlss = true;
                   }
               }
               else
               {
                   device_data.sr_output_color = output_color;
               }

               if (!skip_dlss)
               {
                   game_device_data.sr_source_color = nullptr;
                   ps_shader_resources[0]->GetResource(&game_device_data.sr_source_color);
                   game_device_data.depth_buffer = nullptr;
                   ps_shader_resources[3]->GetResource(&game_device_data.depth_buffer);
                   game_device_data.sr_motion_vectors = nullptr;
                   ps_shader_resources[6]->GetResource(&game_device_data.sr_motion_vectors);

                   reshade::log::message(reshade::log::level::info, ("Loading DLSS inputs successfully"));

                   // Extract jitter from constant buffer 0
                   {
                       ID3D11Buffer* cb0_buffer = nullptr;
                       native_device_context->PSGetConstantBuffers(0, 1, &cb0_buffer); // slot 0 = b0

                       if (cb0_buffer)
                       {
                           D3D11_BUFFER_DESC cb0_desc = {};
                           cb0_buffer->GetDesc(&cb0_desc);

                           ID3D11Buffer* staging_cb0 = cb0_buffer;
                           com_ptr<ID3D11Buffer> staging_cb0_buf;
                           if (cb0_desc.Usage != D3D11_USAGE_STAGING || !(cb0_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
                           {
                               cb0_desc.Usage = D3D11_USAGE_STAGING;
                               cb0_desc.BindFlags = 0;
                               cb0_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                               cb0_desc.MiscFlags = 0;
                               cb0_desc.StructureByteStride = 0;
                               HRESULT hr_staging = native_device->CreateBuffer(&cb0_desc, nullptr, &staging_cb0_buf);
                               if (SUCCEEDED(hr_staging))
                               {
                                   native_device_context->CopyResource(staging_cb0_buf.get(), cb0_buffer);
                                   staging_cb0 = staging_cb0_buf.get();
                                   D3D11_MAPPED_SUBRESOURCE mapped_cb0 = {};
                                   if (SUCCEEDED(native_device_context->Map(staging_cb0, 0, D3D11_MAP_READ, 0, &mapped_cb0)))
                                   {
                                       // cb0 is float4[140], so each element is 16 bytes
                                       const float* cb0_floats = reinterpret_cast<const float*>(mapped_cb0.pData);
                                       //float4 g_screenSize : packoffset(c0);
                                       //float4 g_frameBits : packoffset(c1);
                                       //float4 g_uvJitterOffset : packoffset(c2);
                                       float jitter_x = cb0_floats[8];
                                       float jitter_y = cb0_floats[9];
                                       if (jitter_x != 0 || jitter_y != 0)
                                       {
                                           projection_jitters.x = jitter_x;
                                           projection_jitters.y = jitter_y;
                                       }
                                       native_device_context->Unmap(staging_cb0, 0);
                                       staging_cb0->Release();
                                       cb0_buffer->Release();
                                   }
                               }
                               else
                               {
                                   cb0_buffer->Release();
                               }
                           }
                       }
                   }
                   reshade::log::message(reshade::log::level::info, ("Loading DLSS  successfully"));
                   reshade::log::message(
                       reshade::log::level::info,
                       ("Jitter X: " + std::to_string(projection_jitters.x) +
                           ", Jitter Y: " + std::to_string(projection_jitters.y)).c_str()
                   );

                   bool reset_dlss = device_data.force_reset_sr || dlss_output_changed;
                   device_data.force_reset_sr = false;

                   float dlss_pre_exposure = 0.f;
                   SR::SuperResolutionImpl::DrawData draw_data;
                   draw_data.source_color = game_device_data.sr_source_color.get();
                   draw_data.output_color = device_data.sr_output_color.get();
                   draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
                   draw_data.depth_buffer = game_device_data.depth_buffer.get();
                   draw_data.pre_exposure = dlss_pre_exposure;
#if 1
                   draw_data.jitter_x = projection_jitters.x;
                   draw_data.jitter_y = projection_jitters.y;
#else // TODO
                   draw_data.jitter_x = projection_jitters.x * device_data.render_resolution.x * -0.5f;
                   draw_data.jitter_y = projection_jitters.y * device_data.render_resolution.y * -0.5f;
#endif
                   draw_data.reset = reset_dlss;

                   bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                   reshade::log::message(
                       reshade::log::level::info,
                       (std::string("DLSS succeeded: ") + (dlss_succeeded ? "true" : "false")).c_str()
                   );

                   if (dlss_succeeded)
                   {
                       device_data.has_drawn_sr = true;
                   }

                   game_device_data.sr_motion_vectors_rtv = nullptr;
                   game_device_data.sr_motion_vectors = nullptr;
                   game_device_data.sr_source_color = nullptr;
                   game_device_data.depth_buffer = nullptr;

                   ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
                   native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());

                   if (device_data.has_drawn_sr)
                   {
                       if (!dlss_output_supports_uav)
                       {
                           native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                       }
                       else
                       {
                           device_data.sr_output_color = nullptr;
                       }

                       reshade::log::message(
                           reshade::log::level::info,
                           "Skipping the TAA draw call ..."
                       );

                       return DrawOrDispatchOverrideType::Replaced;
                   }
                   else
                   {
                       //ASSERT_ONCE(false);
                       //cb_luma_frame_settings.SRType = 0;
                       //device_data.cb_luma_frame_settings_dirty = true;
                       //device_data.sr_suppressed = true;
                       device_data.force_reset_sr = true;
                   }

               }
               if (dlss_output_supports_uav)
               {
                   device_data.sr_output_color = nullptr;
               }


           }
		   

	   }
      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call

   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
       device_data.game = new GameDeviceDataFFXV;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
       auto& game_device_data = GetGameDeviceData(device_data);

       if (!game_device_data.has_drawn_upscaling)
       {
          device_data.force_reset_sr = true; // If the frame didn't draw the scene, DLSS needs to reset to prevent the old history from blending with the new scene
       }
       game_device_data.has_drawn_upscaling = false;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("FFXV Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Final Fantasy XV Luma Edition");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("75DFE4B0", nullptr, 16)); // Main game tonemapping
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("18EF8C72", nullptr, 16)); // Title screen tonemapping
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("DD4C5B74", nullptr, 16)); // Post-processing / swapchain

      // TODO: intercept this
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("0DF0A97D", nullptr, 16));

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled; // We don't need swapchain upgrade for this game
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;  // 1 = scrgb
      
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)\n2 - Raw HDR (Untonemapped)\nThe HDR tonemapper works for SDR too\nThis games uses a filmic tonemapper, which slightly crushes blacks"},
      };

      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
      
      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            //reshade::api::format::r8g8b8a8_unorm_srgb,
            //reshade::api::format::r8g8b8a8_typeless,
            //reshade::api::format::r8g8b8x8_unorm,
            //reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            //reshade::api::format::b8g8r8a8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_typeless,
            //reshade::api::format::b8g8r8x8_unorm,
            //reshade::api::format::b8g8r8x8_unorm_srgb,
            //reshade::api::format::b8g8r8x8_typeless,

            reshade::api::format::r11g11b10_float,
            reshade::api::format::r10g10b10a2_typeless,
            reshade::api::format::r10g10b10a2_unorm,
            //reshade::api::format::r16g16_float,
            //reshade::api::format::r16g16_unorm,
            //reshade::api::format::r32_g8_typeless
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      game = new FinalFantasyXV();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}