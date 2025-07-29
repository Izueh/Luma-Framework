#define GAME_DISHONORED_2 1

#define UPGRADE_SAMPLERS 0
#define ENABLE_NGX 1
// Hangs on boot
#define DISABLE_AUTO_DEBUGGER
// Disabled as it makes boot extremely slow
#define ALLOW_SHADERS_DUMPING 0

// Not used by Dishonored 2?
#define ENABLE_SHADER_CLASS_INSTANCES 1

#include "..\..\Core\core.hpp"

// Hack: we need to include this cpp file here because it's part of the core library but we actually don't include it as a library, due to limitations (see the game template for more)
#include "..\..\Core\dlss\DLSS.cpp"

struct CBPerViewGlobals
{
   float4 cb_alwaystweak;
   float4 cb_viewrandom;
   Matrix44F cb_viewprojectionmatrix;
   Matrix44F cb_viewmatrix;
   // apparently zero? seemengly unrelated from jitters
   float4 cb_subpixeloffset;
   Matrix44F cb_projectionmatrix;
   Matrix44F cb_previousviewprojectionmatrix;
   Matrix44F cb_previousviewmatrix;
   Matrix44F cb_previousprojectionmatrix;
   float4 cb_mousecursorposition;
   float4 cb_mousebuttonsdown;
   // xy and the jitter offsets in uv space (y is flipped), zw might be the same in another space or the ones from the previous frame
   float4 cb_jittervectors;
   Matrix44F cb_inverseviewprojectionmatrix;
   Matrix44F cb_inverseviewmatrix;
   Matrix44F cb_inverseprojectionmatrix;
   float4 cb_globalviewinfos;
   float3 cb_wscamforwarddir;
   uint cb_alwaysone;
   float3 cb_wscamupdir;
   // This seems to be true at all times for TAA
   uint cb_usecompressedhdrbuffers;
   float3 cb_wscampos;
   float cb_time;
   float3 cb_wscamleftdir;
   float cb_systime;
   float2 cb_jitterrelativetopreviousframe;
   float2 cb_worldtime;
   float2 cb_shadowmapatlasslicedimensions;
   float2 cb_resolutionscale;
   float2 cb_parallelshadowmapslicedimensions;
   float cb_framenumber;
   uint cb_alwayszero;
};

namespace
{
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_UpscaleSharpen;
   ShaderHashesList shader_hashes_DownsampleDepth;
   ShaderHashesList shader_hashes_UnprojectDepth;

#if DEVELOPMENT
   std::thread::id global_cbuffer_thread_id;
#endif

   // Directly from cbuffer (so these are transposed)
   Matrix44F projection_matrix;
   Matrix44F nearest_projection_matrix; // For first person weapons (view model)
   Matrix44F previous_projection_matrix;
   Matrix44F previous_nearest_projection_matrix;
   float2 previous_projection_jitters = { 0, 0 };
   float2 projection_jitters = { 0, 0 };
   CBPerViewGlobals cb_per_view_global = { };
   CBPerViewGlobals cb_per_view_global_previous = cb_per_view_global;

#if DEVELOPMENT
   std::vector<std::string> cb_per_view_globals_last_drawn_shader; // Not exactly thread safe but it's fine...
   std::vector<CBPerViewGlobals> cb_per_view_globals;
   std::vector<CBPerViewGlobals> cb_per_view_globals_previous;
#endif

   // Dev or User settings:
#if DEVELOPMENT
   float dlss_custom_exposure = 0.f; // Ignored at 0
   float dlss_custom_pre_exposure = 0.f; // Ignored at 0
   int dlss_test = 0; //TODOFT
   int force_taa_jitter_phases = 0; // Ignored if 0 (automatic mode), set to 1 to basically disable jitters
   bool disable_taa_jitters = false;
#endif
}

struct GameDeviceDataDishonored2 final : public GameDeviceData
{
   // If this is valid, the game's TAA was running on a deferred command list, and thus we delay DLSS
   std::atomic<void*> dlss_sr_deferred_command_list = nullptr;
   com_ptr<ID3D11Resource> dlss_source_color;
   com_ptr<ID3D11Resource> dlss_motion_vectors;
   //com_ptr<ID3D11Texture2D> dlss_output_color_2; //TODOFT: delete this and related code

   // Game state
   com_ptr<ID3D11Resource> depth_buffer;

   std::atomic<bool> has_drawn_scene = false; // This is set early in the frame, as soon as we detect that the 3D scene is rendering (it won't be made true only when it finished rendering)
   std::atomic<void*> final_post_process_command_list = nullptr;
   bool has_drawn_scene_previous = false;

   bool found_per_view_globals = false;
   bool has_drawn_dlss_sr = false;

   std::atomic<bool> prey_drs_active = false;
   std::atomic<bool> prey_drs_detected = false;
   std::atomic<bool> prey_taa_active = false; // Instant version of "taa_detected".
   // Index 0 is one frame ago, index 1 is two frames ago
   bool previous_prey_taa_active[2] = { false, false };
};

//TODOFT: Building shaders menu (apply gamma?)
class Dishonored2 final : public Game
{
public:
   static const GameDeviceDataDishonored2& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataDishonored2*>(device_data.game);
   }
   static GameDeviceDataDishonored2& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataDishonored2*>(device_data.game);
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(Dishonored2::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(Dishonored2::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(Dishonored2::OnExecuteSecondaryCommandList);
      }
   }

   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('3');
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataDishonored2;
   }

   //TODOFT: delete?
   void UpdateLumaInstanceDataCB(LumaInstanceData& data) override
   {
   }

   bool UpdateGlobalCB(const void* global_buffer_data_ptr, reshade::api::device* device) override
   {
      const CBPerViewGlobals& global_buffer_data = *((const CBPerViewGlobals*)global_buffer_data_ptr);

      bool is_valid_cbuffer = true;

#if DEVELOPMENT && 0
      cb_per_view_globals.emplace_back(global_buffer_data);
      cb_per_view_globals_last_drawn_shader.emplace_back(last_drawn_shader); // The shader hash could we unspecified if we didn't replace the shader
#endif // DEVELOPMENT

      if (!is_valid_cbuffer)
      {
         return false;
      }

      // Shadow maps and other things temporarily change the values in the global cbuffer,
      // like not use inverse depth (which affects the projection matrix, and thus many other matrices?),
      // use different render and output resolutions, etc etc.
      // We could also base our check on "CV_ProjRatio" (x and y) and "CV_FrustumPlaneEquation" and "CV_DecalZFightingRemedy" as these are also different for alternative views.
      // "CV_PrevViewProjMatr" is not a raw projection matrix when rendering shadow maps, so we can easily detect that.
      // Note: we can check if the matrix is identity to detect whether we are currently in a menu (the main menu?)
      bool is_custom_draw_version = !MatrixIsProjection(global_buffer_data.cb_projectionmatrix);

      if (is_custom_draw_version)
      {
         return false;
      }

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT
      std::thread::id new_global_cbuffer_thread_id = std::this_thread::get_id();
      // Make sure this cbuffer is always updated in the same thread (forever)
      if (global_cbuffer_thread_id != std::thread::id())
      {
         ASSERT_ONCE(global_cbuffer_thread_id == new_global_cbuffer_thread_id);
      }
      global_cbuffer_thread_id = new_global_cbuffer_thread_id;
#endif

      // Copy the temporary buffer ptr into our persistent data
      cb_per_view_global = global_buffer_data;

      auto current_projection_matrix = cb_per_view_global.cb_projectionmatrix;
      if (!game_device_data.has_drawn_scene_previous)
      {
         previous_projection_matrix = current_projection_matrix;
      }

      // Update our cached data with information from the cbuffer.
      // After vanilla tonemapping (as soon as AA starts),
      // camera jitters are removed from the cbuffer projection matrices, and the render resolution is also set to 100% (after the upscaling pass),
      // so we want to ignore these cases. We stop at the gbuffer compositions draw, because that's the last know cbuffer 13 to have the perfect values we are looking for (that shader is always run, so it's reliable)!
      // A lot of passes are drawn on scaled down render targets and the cbuffer values would have been updated to reflect that (e.g. "CV_ScreenSize"), so ignore these cases.
      if (!game_device_data.found_per_view_globals)
      {
#if DEVELOPMENT
         static float2 local_previous_render_resolution;
         if (!game_device_data.found_per_view_globals)
         {
            local_previous_render_resolution.x = device_data.output_resolution.x * cb_per_view_global.cb_resolutionscale.x;
            local_previous_render_resolution.y = device_data.output_resolution.y * cb_per_view_global.cb_resolutionscale.y;
         }
#endif // DEVELOPMENT

         //TODOFT: these have read/writes that are possibly not thread safe but they should never cause issues in actual usages of Prey
         device_data.render_resolution.x = device_data.output_resolution.x * cb_per_view_global.cb_resolutionscale.x; // TODO: round or floor?
         device_data.render_resolution.y = device_data.output_resolution.y * cb_per_view_global.cb_resolutionscale.y;

         auto previous_prey_drs_active = game_device_data.prey_drs_active.load();
         game_device_data.prey_drs_active = std::abs(device_data.render_resolution.x - device_data.output_resolution.x) >= 0.5f || std::abs(device_data.render_resolution.y - device_data.output_resolution.y) >= 0.5f;
         // Make sure this doesn't change within a frame (once we found DRS in a frame, we should never "lose" it again for that frame.
         // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
         ASSERT_ONCE(device_data.cloned_pipeline_count == 0 || !game_device_data.found_per_view_globals || !previous_prey_drs_active || (previous_prey_drs_active == game_device_data.prey_drs_active));

#if DEVELOPMENT
         // Make sure that our rendering resolution doesn't change randomly within the pipeline (it probably will, it seems to trigger during quick save loads, maybe for the very first draw call to clear buffers)
         const float2 previous_render_resolution = local_previous_render_resolution;
         ASSERT_ONCE(!game_device_data.has_drawn_scene_previous || !game_device_data.found_per_view_globals || !game_device_data.prey_drs_detected || (AlmostEqual(device_data.render_resolution.x, previous_render_resolution.x, 0.25f) && AlmostEqual(device_data.render_resolution.y, previous_render_resolution.y, 0.25f)));
#endif // DEVELOPMENT

         // Once we detect the user enabled DRS, we can't ever know it's been disabled because the game only occasionally drops to lower rendering resolutions, so we couldn't know if it was ever disabled
         if (game_device_data.prey_drs_active)
         {
            device_data.dlss_sr_suppressed = true;
            game_device_data.prey_drs_detected = true;

            float resolution_scale = device_data.render_resolution.y / device_data.output_resolution.y;
            // Lower the DLSS quality mode (which might introduce a stutter, or a slight blurring of the image as it resets the history),
            // but this will make DLSS not use DLAA and instead fall back on a quality mode that allows for a dynamic range of resolutions.
            // This isn't the exact rend resolution DLSS will be forced to use, but the center of a range it's gonna expect.
            // Unfortunately DLSS has a limited range of accepted resolutions per quality mode, and if you go beyond it, it fails to render (until in range again),
            // thus, we need to make sure the automatic DRS range of Prey is within the same range!
            // We couldn't change this resolution scale every frame as it's make DLSS stutter massively.
            // See CryEngine "osm_fbMinScale" cvar (config), that drives the min rend res scale, the DLSS rend scale should ideally be set to the same value, but it's fine if it's above it, given it's the target "average" dynamic resolution.
            // If CryEngine ever went below 50% render scale, we force DLSS into ultra performance mode (33%), as the range allowed by quality mode (67%) can't go below 50%. There will be a stutter (and history reset?) every time we swap back and forth, but at least it works...
            if (resolution_scale < 0.5f - FLT_EPSILON)
            {
#if 1 // Unfortunately no quality mode with a res scale below 0.5 supports dynamic resolution scaling, so we are forced to change the quality mode every frame or so (or at least, every time Prey changes DRS value, which might further slow down the DRS detection mechanism...)
               device_data.dlss_render_resolution_scale = resolution_scale;
#else // If we do this, DLSS would fail if any resolution that didn't exactly match 33% render scale was used by the game
               device_data.dlss_render_resolution_scale = 1.f / 3.f;
#endif
            }
            else
            {
               // This should pick quality or balanced mode, with a range from 100% to 50% resolution scale
               device_data.dlss_render_resolution_scale = 1.f / 1.5f;
            }
         }
         // Reset to DLAA and try again (once), given that we can't go from a 1/3 to a 1 rend scale (e.g. in case DRS was disabled in the menu)
         else if (false && device_data.dlss_sr_suppressed && device_data.dlss_render_resolution_scale != 1.f)
         {
            device_data.dlss_render_resolution_scale = 1.f;
            device_data.dlss_sr_suppressed = false;
         }

         // NOTE: we could just save the first one we found, it should always be jittered and "correct".
         projection_matrix = current_projection_matrix;

         const auto projection_jitters_copy = projection_jitters;

         // The matrix is transposed so we flip the matrix x and y indices.
         projection_jitters.x = current_projection_matrix(2, 0);
         projection_jitters.y = current_projection_matrix(2, 1);
         // TODO: cb_per_view_global.cb_jittervectors?

#if DEVELOPMENT
         ASSERT_ONCE(disable_taa_jitters || (projection_jitters_copy.x == 0 && projection_jitters_copy.y == 0) || (projection_jitters.x != 0 || projection_jitters.y != 0)); // Once we found jitters, we should never cache matrices that don't have jitters anymore
#endif

         bool prey_taa_active_copy = game_device_data.prey_taa_active;
         // This is a reliable check to tell whether TAA is enabled. Jitters are "never" zero if they are enabled:
         // they can be if we use the "srand" method, but it would happen one in a billion years;
         // they could also be zero with Halton if the frame index was reset to zero (it is every x frames), but that happens very rarely, and for one frame only (we have two frames as tolerance).
         game_device_data.prey_taa_active = (std::abs(projection_jitters.x * device_data.render_resolution.x) >= 0.00075) || (std::abs(projection_jitters.y * device_data.render_resolution.y) >= 0.00075); //TODOFT: make calculations more accurate (the threshold), especially with higher Halton phases!
#if DEVELOPMENT
         game_device_data.prey_taa_active = game_device_data.prey_taa_active || disable_taa_jitters;
#endif // DEVELOPMENT
         // Make sure that once we detect that TAA was active within a frame, then it should never be detected as off in the same frame (it would mean we are reading a bad cbuffer 13 that we should have discarded).
         // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
         ASSERT_ONCE(device_data.cloned_pipeline_count == 0 || !game_device_data.found_per_view_globals || !prey_taa_active_copy || (prey_taa_active_copy == game_device_data.prey_taa_active));
         if (prey_taa_active_copy != game_device_data.prey_taa_active && game_device_data.has_drawn_scene_previous) // TAA changed
         {
            // Detect if TAA was ever detected as on/off/on or off/on/off over 3 frames, because if that was so, our jitter "length" detection method isn't solid enough and we should do more (or add more tolernace to it),
            // this might even happen every x hours once the randomization triggers specific enough values, though all TAA modes have a pretty short cycle with fixed jitters,
            // so it should either happen quickly or never.
            bool middle_value_different = (game_device_data.prey_taa_active == game_device_data.previous_prey_taa_active[0]) != (game_device_data.prey_taa_active == game_device_data.previous_prey_taa_active[1]);
            ASSERT_ONCE(!middle_value_different);
         }
         bool drew_dlss = cb_luma_frame_settings.DLSS; // If this was true, DLSS would have been enabled and probably drew
         device_data.taa_detected = game_device_data.prey_taa_active || game_device_data.previous_prey_taa_active[0]; // This one has a two frames tolerance. We let it persist even if the game stopped drawing the 3D scene.
         cb_luma_frame_settings.DLSS = (device_data.dlss_sr && !device_data.dlss_sr_suppressed && device_data.taa_detected) ? 1 : 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
         device_data.cb_luma_frame_settings_dirty |= (bool)cb_luma_frame_settings.DLSS != drew_dlss;
         if (cb_luma_frame_settings.DLSS && !drew_dlss)
         {
            // Reset DLSS history when we toggle DLSS on and off manually, or when the user in the game changes the AA mode,
            // otherwise the history from the last time DLSS was active will be kept (DLSS doesn't know time passes since it was last used).
            // We could also clear DLSS resources here when we know it's unused for a while, but it would possibly lead to stutters.
            device_data.force_reset_dlss_sr = true;
         }

#if DEVELOPMENT
         if (!custom_texture_mip_lod_bias_offset)
#endif
         {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);

            const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
            if (device_data.dlss_sr && !device_data.dlss_sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
            {
               device_data.texture_mip_lod_bias_offset = std::log2(device_data.render_resolution.y / device_data.output_resolution.y) - 1.f; // This results in -1 at output res
            }
            else
            {
               // Reset to best fallback value.
               // This bias offset replaces the value from the game (see "samplers_upgrade_mode" 5), which was based on the "r_AntialiasingTSAAMipBias" cvar for most textures (it doesn't apply to all the ones that would benefit from it, and still applies to ones that exhibit moire patterns),
               // but only if TAA was engaged (not SMAA or SMAA+TAA) (it might persist on SMAA after once using TAA, due to a bug).
               // Prey defaults that to 0 but Luma's configs set it to -1.
               device_data.texture_mip_lod_bias_offset = device_data.taa_detected ? -1.f : 0.f;
            }
            const auto new_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;

            bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
            // Re-create all samplers immediately here instead of doing it at the end of the frame.
            // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
            // It also allows us to use the right samplers for this frame's resolution.
            if (texture_mip_lod_bias_offset_changed)
            {
               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
               {
                  if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset)) continue; // Skip "resolutions" that already got their custom samplers created
                  ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  shared_lock_samplers.unlock(); // This is fine!
                  {
                     std::unique_lock unique_lock_samplers(s_mutex_samplers);
                     samplers_handle.second[new_texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, native_device, native_desc);
                  }
                  shared_lock_samplers.lock();
               }
            }
         }

         if (!game_device_data.has_drawn_scene_previous)
         {
            device_data.previous_render_resolution = device_data.render_resolution;
            previous_projection_jitters = projection_jitters;

            // Set it to the latest value (ignoring the actual history)
            game_device_data.previous_prey_taa_active[0] = game_device_data.prey_taa_active;
            game_device_data.previous_prey_taa_active[1] = game_device_data.prey_taa_active;
         }

         game_device_data.found_per_view_globals = true;
      }

      return true;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.has_drawn_scene_previous = game_device_data.has_drawn_scene;
      game_device_data.has_drawn_scene = false;

      //TODOFT: do this in the super?
      device_data.has_drawn_main_post_processing_previous = device_data.has_drawn_main_post_processing;
      device_data.has_drawn_main_post_processing = false;

      game_device_data.found_per_view_globals = false;
      game_device_data.has_drawn_dlss_sr = false;
   }

   bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      D3D11_TEXTURE2D_DESC depth_desc; // TODO
#if 1
      if (!game_device_data.has_drawn_scene && original_shader_hashes.Contains(shader_hashes_DownsampleDepth))
      {
         game_device_data.has_drawn_scene = true;
         //game_device_data.has_drawn_post_process = true; //TODOFT: already done later?

         com_ptr<ID3D11ShaderResourceView> depth_srv;
         native_device_context->CSGetShaderResources(0, 1, &depth_srv);

         com_ptr<ID3D11UnorderedAccessView> depth_uav;
         native_device_context->CSGetUnorderedAccessViews(3, 1, &depth_uav);

         game_device_data.depth_buffer = nullptr;
         if (depth_srv)
         {
            depth_srv->GetResource(&game_device_data.depth_buffer);

            com_ptr<ID3D11Texture2D> depth_2d;
            HRESULT hr = game_device_data.depth_buffer->QueryInterface(&depth_2d);
            ASSERT_ONCE(SUCCEEDED(hr));
            depth_2d->GetDesc(&depth_desc);
         }
      }
#else
      if (!game_device_data.has_drawn_scene && original_shader_hashes.Contains(shader_hashes_UnprojectDepth) && false)
      {
         game_device_data.has_drawn_scene = true;

         com_ptr<ID3D11ShaderResourceView> depth_opaque_srv;
         native_device_context->CSGetShaderResources(1, 1, &depth_opaque_srv);
         com_ptr<ID3D11ShaderResourceView> depth_alpha_srv;
         native_device_context->CSGetShaderResources(0, 1, &depth_alpha_srv);

         com_ptr<ID3D11UnorderedAccessView> depth_uav;
         native_device_context->CSGetUnorderedAccessViews(0, 1, &depth_uav); // float4

         game_device_data.depth_buffer = nullptr;
         if (depth_opaque_srv)
         {
            depth_opaque_srv->GetResource(&game_device_data.depth_buffer);

            com_ptr<ID3D11Texture2D> depth_2d;
            HRESULT hr = game_device_data.depth_buffer->QueryInterface(&depth_2d);
            ASSERT_ONCE(SUCCEEDED(hr));
            depth_2d->GetDesc(&depth_desc);
         }
      }
#endif

      if (!device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_UpscaleSharpen))
      {
         if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         {
            game_device_data.final_post_process_command_list = native_device_context;
         }
         else
         {
            device_data.has_drawn_main_post_processing = true;
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_TAA))
      {
         // Not thread safe?
         device_data.cb_per_view_global_buffer = nullptr;
         native_device_context->CSGetConstantBuffers(1, 1, &device_data.cb_per_view_global_buffer);
      }

   #if ENABLE_NGX
      // DLSS upscaling/TAA
      if (device_data.dlss_sr && !device_data.dlss_sr_suppressed && original_shader_hashes.Contains(shader_hashes_TAA))
      {
         com_ptr<ID3D11ShaderResourceView> srvs[2]; // TODO: rename
         // 1 motion vectors
         // 2 color source (pre TAA, jittered)
         native_device_context->CSGetShaderResources(1, ARRAYSIZE(srvs), reinterpret_cast<ID3D11ShaderResourceView**>(srvs));

         com_ptr<ID3D11UnorderedAccessView> uav;
         native_device_context->CSGetUnorderedAccessViews(1, 1, &uav);

         const bool dlss_inputs_valid = srvs[0].get() != nullptr && srvs[1].get() != nullptr && uav.get() != nullptr; // We don't check for "device_data.depth_buffer" here yet as this can happen before in a deferred context
         ASSERT_ONCE(dlss_inputs_valid);
         if (dlss_inputs_valid)
         {
            com_ptr<ID3D11Resource> output_colorTemp;
            uav->GetResource(&output_colorTemp);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_colorTemp->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC output_texture_desc;
            output_color->GetDesc(&output_texture_desc);

   #if FORCE_SMAA_MIPS // Define from the native plugin (not ever defined here!)
            ASSERT_ONCE(output_texture_desc.MipLevels > 1); // To improve "Perfect Perspective" lens distortion
   #endif

            ASSERT_ONCE(std::lrintf(device_data.output_resolution.x) == output_texture_desc.Width && std::lrintf(device_data.output_resolution.y) == output_texture_desc.Height);
            std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Height * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
            // The "HDR" flag in DLSS SR actually means whether the color is in linear space or "sRGB gamma" (apparently not 2.2) (SDR) space, colors beyond 0-1 don't seem to be clipped either way
            //bool dlss_hdr = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1; // we are assuming the value was always a number and not empty
            bool dlss_hdr = true;

            bool delay_dlss = native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE;

            //native_device->GetImmediateContext(&native_device_context);

            // TODO: we could do this async from the beginning of rendering (when we can detect res changes), to here, with a mutex, to avoid potential stutters when DRS first engages (same with creating DLSS textures?) or changes resolution? (we could allow for creating more than one DLSS feature???)
            // 
            // Our DLSS implementation picks a quality mode based on a fixed rendering resolution, but we scale it back in case we detected the game is running DRS, otherwise we run DLAA.
            // At lower quality modes (non DLAA), DLSS actually seems to allow for a wider input resolution range that it actually claims when queried for it, but if we declare a resolution scale below 50% here, we can get an hitch,
            // still, DLSS will keep working at any input resolution (or at least with a pretty big tolerance range).
            // This function doesn't alter the pipeline state (e.g. shaders, cbuffers, RTs, ...), if not, we need to move it to the "Present()" function, it doesn't seem like we can do it async though (DLSS rendering crashes in deferred context, possibly this would too)
            if (!delay_dlss)
            {
               NGX::DLSS::UpdateSettings(device_data.dlss_sr_handle, native_device_context, output_texture_desc.Width, output_texture_desc.Height, dlss_render_resolution[0], dlss_render_resolution[1], dlss_hdr, game_device_data.prey_drs_detected);
            }

            bool skip_dlss = output_texture_desc.Width < 32 || output_texture_desc.Height < 32; // DLSS doesn't support output below 32x32
            bool dlss_output_changed = false;
            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
            if (!dlss_output_supports_uav)
            {
               ASSERT_ONCE(!dlss_use_native_uav); // Should never happen anymore

               output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.dlss_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
                  device_data.dlss_output_color->GetDesc(&dlss_output_texture_desc);
                  dlss_output_changed = dlss_output_texture_desc.Width != output_texture_desc.Width || dlss_output_texture_desc.Height != output_texture_desc.Height || dlss_output_texture_desc.Format != output_texture_desc.Format;
               }
               if (!device_data.dlss_output_color.get() || dlss_output_changed)
               {
                  device_data.dlss_output_color = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&output_texture_desc, nullptr, &device_data.dlss_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
               if (!device_data.dlss_output_color.get())
               {
                  skip_dlss = true;
               }
            }
            else
            {
               device_data.dlss_output_color = output_color;
            }

            if (!skip_dlss)
            {
               game_device_data.dlss_source_color = nullptr;
               game_device_data.dlss_motion_vectors = nullptr;
               srvs[1]->GetResource(&game_device_data.dlss_source_color);
               srvs[0]->GetResource(&game_device_data.dlss_motion_vectors);

               // TODO
               com_ptr<ID3D11Texture2D> source_color_2d;
               HRESULT hr = game_device_data.dlss_source_color->QueryInterface(&source_color_2d);
               ASSERT_ONCE(SUCCEEDED(hr));
               D3D11_TEXTURE2D_DESC source_color_2d_desc;
               source_color_2d->GetDesc(&source_color_2d_desc);
               com_ptr<ID3D11Texture2D> motion_vectors_2d;
               hr = game_device_data.dlss_motion_vectors->QueryInterface(&motion_vectors_2d);
               ASSERT_ONCE(SUCCEEDED(hr));
               D3D11_TEXTURE2D_DESC motion_vectors_2d_desc;
               motion_vectors_2d->GetDesc(&motion_vectors_2d_desc);

               // Generate "fake" exposure texture
               bool exposure_changed = false;
               float dlss_scene_exposure = device_data.dlss_scene_exposure;
   #if DEVELOPMENT
               if (dlss_custom_exposure > 0.f)
               {
                  dlss_scene_exposure = dlss_custom_exposure;
               }
   #endif // DEVELOPMENT
               exposure_changed = dlss_scene_exposure != device_data.dlss_exposure_texture_value;
               device_data.dlss_exposure_texture_value = dlss_scene_exposure;
               // TODO: optimize this for the "DLSS_RELATIVE_PRE_EXPOSURE" false case! Avoid re-creating the texture every frame the exposure changes and instead make it dynamic and re-write it from the CPU? Or simply make our exposure calculation shader write to a texture directly
               // (though in that case it wouldn't have the same delay as the CPU side pre-exposure buffer readback)
               if (!device_data.dlss_exposure.get() || exposure_changed)
               {
                  D3D11_TEXTURE2D_DESC exposure_texture_desc; // DLSS fails if we pass in a 1D texture so we have to make a 2D one
                  exposure_texture_desc.Width = 1;
                  exposure_texture_desc.Height = 1;
                  exposure_texture_desc.MipLevels = 1;
                  exposure_texture_desc.ArraySize = 1;
                  exposure_texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT; // FP32 just so it's easier to initialize data for it
                  exposure_texture_desc.SampleDesc.Count = 1;
                  exposure_texture_desc.SampleDesc.Quality = 0;
                  exposure_texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
                  exposure_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                  exposure_texture_desc.CPUAccessFlags = 0;
                  exposure_texture_desc.MiscFlags = 0;

                  // It's best to force an exposure of 1 given that DLSS runs after the auto exposure is applied (in tonemapping).
                  // Theoretically knowing the average exposure of the frame would still be beneficial to it (somehow) so maybe we could simply let the auto exposure in,
                  D3D11_SUBRESOURCE_DATA exposure_texture_data;
                  exposure_texture_data.pSysMem = &dlss_scene_exposure; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
                  exposure_texture_data.SysMemPitch = 32;
                  exposure_texture_data.SysMemSlicePitch = 32;

                  device_data.dlss_exposure = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&exposure_texture_desc, &exposure_texture_data, &device_data.dlss_exposure);
                  assert(SUCCEEDED(hr));
               }

               // Reset DLSS history if we did not draw motion blur (and we previously did). Based on CryEngine source code, mb is skipped on the first frame after scene cuts, so we want to re-use that information (this works even if MB was disabled).
               // Reset DLSS history if for one frame we had stopped tonemapping. This might include some scene cuts, but also triggers when entering full screen UI menus or videos and then leaving them (it shouldn't be a problem).
               // Reset DLSS history if the output resolution or format changed (just an extra safety mechanism, it might not actually be needed).
               bool reset_dlss = device_data.force_reset_dlss_sr || dlss_output_changed;
               if (!delay_dlss)
               {
                  device_data.force_reset_dlss_sr = false;
               }

               uint32_t render_width_dlss = std::lrintf(device_data.render_resolution.x);
               uint32_t render_height_dlss = std::lrintf(device_data.render_resolution.y);

               // These configurations store the image already multiplied by paper white from the beginning of tonemapping, including at the time DLSS runs.
               // The other configurations run DLSS in "SDR" Gamma Space so we couldn't safely change the exposure.
               const bool dlss_use_paper_white_pre_exposure = false; // Not needed for DH2, DLSS is before TM
               //const bool dlss_use_paper_white_pre_exposure = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1;

               float dlss_pre_exposure = 0.f; // 0 means it's ignored
               if (dlss_use_paper_white_pre_exposure)
               {
   #if 1 // Alternative that considers a value of 1 in the DLSS color textures to match the SDR output nits range (whatever that is)
                  dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / default_paper_white;
   #else // Alternative that considers a value of 1 in the DLSS color textures to match 203 nits
                  dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / srgb_white_level;
   #endif
                  dlss_pre_exposure *= device_data.dlss_scene_pre_exposure;
               }
   #if DEVELOPMENT
               if (dlss_custom_pre_exposure > 0.f)
                  dlss_pre_exposure = dlss_custom_pre_exposure;
   #endif

               // Clean up random stuff to default values and copy resources to make sure it's all clear
               //dlss_pre_exposure = 1.0;
               //device_data.dlss_exposure = nullptr;
               ////game_device_data.depth_buffer = nullptr;
               //reset_dlss = false;
               //projection_jitters.x = 0;
               //projection_jitters.y = 0;
               //device_data.dlss_output_color_2 = CloneTexture2D(cmd_list, device_data.dlss_output_color.get(), false);
               //device_data.source_color_2 = CloneTexture2D(cmd_list, dlss_source_color.get(), false);
               //device_data.motion_vectors_2 = CloneTexture2D(cmd_list, dlss_motion_vectors.get(), false);
               //game_device_data.dlss_source_color = (ID3D11Texture2D*)dlss_source_color.get();
               //game_device_data.dlss_motion_vectors = (ID3D11Texture2D*)dlss_motion_vectors.get();

               // There doesn't seem to be a need to restore the DX state to whatever we had before (e.g. render targets, cbuffers, samplers, UAVs, texture shader resources, viewport, scissor rect, ...), CryEngine always sets everything it needs again for every pass.
               // DLSS internally keeps its own frames history, we don't need to do that ourselves (by feeding in an output buffer that was the previous frame's output, though we do have that if needed, it should be in srvs[1]).
               if (!delay_dlss && NGX::DLSS::Draw(device_data.dlss_sr_handle, native_device_context, device_data.dlss_output_color.get(), game_device_data.dlss_source_color.get(), game_device_data.dlss_motion_vectors.get(), game_device_data.depth_buffer.get(), device_data.dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss))
               {
                  device_data.has_drawn_dlss_sr = true;
                  game_device_data.dlss_sr_deferred_command_list = native_device_context;
               }

               if (!delay_dlss)
               {
                  game_device_data.dlss_source_color = nullptr;
                  game_device_data.dlss_motion_vectors = nullptr;
               }

               if (device_data.has_drawn_dlss_sr)
               {
                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.dlss_output_color.get()); // DX11 doesn't need barriers
                  }
                  // In this case it's not our buisness to keep alive this "external" texture
                  else
                  {
                     device_data.dlss_output_color = nullptr;
                  }

                  return true; // "Cancel" the previously set draw call, DLSS has taken care of it
               }
               // DLSS Failed, suppress it for this frame and fall back on SMAA/TAA, hoping that anything before would have been rendered correctly for it already (otherwise it will start being correct in the next frame, given we suppress it (until manually toggled again, given that it'd likely keep failing))
               else if (!delay_dlss)
               {
                  ASSERT_ONCE(false);
                  cb_luma_frame_settings.DLSS = 0;
                  device_data.cb_luma_frame_settings_dirty = true;
                  device_data.dlss_sr_suppressed = true;
                  device_data.force_reset_dlss_sr = true; // We missed frames so it's good to do this, it might also help prevent further errors
               }
               else // "delay_dlss"
               {
                  game_device_data.dlss_sr_deferred_command_list = native_device_context;
                  return true;
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.dlss_output_color = nullptr;
            }
         }
      }
   #endif // ENABLE_NGX
      return false; // Return true to cancel this draw call
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      // No need to convert to native DX11 flags
      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         // There seems to only ever be one buffer type of this size, but it's not guaranteed (we might have found more, but it doesn't matter, they are discarded later)...
         // They seemingly all happen on the same thread.
         // Some how these are not marked as "D3D11_BIND_CONSTANT_BUFFER", probably because it copies them over to some other buffer later?
         if (buffer != nullptr && device_data.cb_per_view_global_buffer == buffer)
         {
#if DEVELOPMENT
            // These are the classic "features" of cbuffer 13 (the one we are looking for), in case any of these were different, it could possibly mean we are looking at the wrong buffer here.
            ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif // DEVELOPMENT
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // We assume this buffer is always unmapped before destruction.
      bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;
      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
      if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
      {
         // The whole buffer size is theoretically "CBPerViewGlobal_buffer_size" but we actually don't have the data for the excessive (padding) bytes,
         // they are never read by shaders on the GPU anyway.
         char global_buffer_data[sizeof(CBPerViewGlobals)];
         std::memcpy(&global_buffer_data[0], device_data.cb_per_view_global_buffer_map_data, sizeof(global_buffer_data));
         if (game->UpdateGlobalCB(&global_buffer_data[0], device))
         {
            // Write back the cbuffer data after we have fixed it up (we always do!)
            std::memcpy(device_data.cb_per_view_global_buffer_map_data, &cb_per_view_global, sizeof(CBPerViewGlobals));
#if DEVELOPMENT
            device_data.cb_per_view_global_buffers.emplace(buffer);
#endif // DEVELOPMENT
         }
         device_data.cb_per_view_global_buffer_map_data = nullptr;
      }
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
#if ENABLE_NGX
      com_ptr<ID3D11DeviceContext> native_device_context;
      com_ptr<ID3D11CommandList> native_command_list;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);
      // ReShade calls this for both DX11's "ExecuteCommandList()" and "FinishCommandList()",
      // we are only interested in the first case, as the second one is irrelevant.
      // "reshade::api::command_list" is a proxy of both "ID3D11DeviceContext" and "ID3D11CommandList",
      // though only for the "ExecuteCommandList()" call the first one passes here will be a "ID3D11DeviceContext" and the second a "ID3D11CommandList",
      // so we branch to make sure we are in that case.
      if (SUCCEEDED(hr))
      {
         device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(&native_command_list);
         if (SUCCEEDED(hr))
         {
            auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            auto& game_device_data = GetGameDeviceData(device_data);
            if (game_device_data.final_post_process_command_list == native_device_context.get())
            {
               game_device_data.final_post_process_command_list = nullptr;
               device_data.has_drawn_main_post_processing = true;
               if (enable_separate_ui_drawing)
               {
                  ID3D11RenderTargetView* const ui_texture_rtv_const = device_data.ui_texture_rtv.get();
                  native_device_context->OMSetRenderTargets(1, &ui_texture_rtv_const, nullptr);
               }
            }

            if (game_device_data.dlss_sr_deferred_command_list != native_command_list.get())
            {
               return;
            }
            game_device_data.dlss_sr_deferred_command_list = nullptr;

            ASSERT_ONCE(game_device_data.depth_buffer.get());

            if (!device_data.dlss_output_color.get())
            {
               ASSERT_ONCE(false); // This shouldn't happen so we don't really handle it properly
               game_device_data.dlss_source_color = nullptr;
               game_device_data.dlss_motion_vectors = nullptr;
               device_data.dlss_output_color = nullptr;
               return;
            }

            // Do "delayed" DLSS:

            std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)device_data.output_resolution.x * (double)device_data.dlss_render_resolution_scale, (double)device_data.output_resolution.y * (double)device_data.dlss_render_resolution_scale, (double)device_data.output_resolution.x / (double)device_data.output_resolution.y);
            // The "HDR" flag in DLSS SR actually means whether the color is in linear space or "sRGB gamma" (apparently not 2.2) (SDR) space, colors beyond 0-1 don't seem to be clipped either way
            bool dlss_hdr = true; // We are assuming the value was always a number and not empty
            NGX::DLSS::UpdateSettings(device_data.dlss_sr_handle, native_device_context.get(), device_data.output_resolution.x, device_data.output_resolution.y, dlss_render_resolution[0], dlss_render_resolution[1], dlss_hdr, game_device_data.prey_drs_detected);

            bool reset_dlss = device_data.force_reset_dlss_sr;
            device_data.force_reset_dlss_sr = false;

            uint32_t render_width_dlss = std::lrintf(device_data.render_resolution.x);
            uint32_t render_height_dlss = std::lrintf(device_data.render_resolution.y);

            float dlss_pre_exposure = 1.0;
            bool dlss_succeeded = NGX::DLSS::Draw(device_data.dlss_sr_handle, native_device_context.get(), device_data.dlss_output_color.get(), game_device_data.dlss_source_color.get(), game_device_data.dlss_motion_vectors.get(), game_device_data.depth_buffer.get(), device_data.dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss);
            ASSERT_ONCE(dlss_succeeded); // We can't restore the original TAA pass at this point (well, we could, but it's pointless, we'll just skip one frame) // TODO: copy the resource instead?

            game_device_data.dlss_source_color = nullptr;
            game_device_data.dlss_motion_vectors = nullptr;
            device_data.dlss_output_color = nullptr;

            if (dlss_succeeded)
            {
               device_data.has_drawn_dlss_sr = true;
            }
            // DLSS Failed, suppress it for this frame and fall back on SMAA/TAA, hoping that anything before would have been rendered correctly for it already (otherwise it will start being correct in the next frame, given we suppress it (until manually toggled again, given that it'd likely keep failing))
            else
            {
               ASSERT_ONCE(false);
               cb_luma_frame_settings.DLSS = 0;
               device_data.cb_luma_frame_settings_dirty = true;
               device_data.dlss_sr_suppressed = true;
               device_data.force_reset_dlss_sr = true; // We missed frames so it's good to do this, it might also help prevent further errors
            }
         }
         return;
      }
      hr = device_child->QueryInterface(&native_command_list);
      {
         device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(&native_device_context);
         if (SUCCEEDED(hr))
         {
            auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            auto& game_device_data = GetGameDeviceData(device_data);
            if (game_device_data.final_post_process_command_list == native_device_context.get())
            {
               game_device_data.final_post_process_command_list = native_command_list.get();
            }
            if (game_device_data.dlss_sr_deferred_command_list == native_device_context.get())
            {
               game_device_data.dlss_sr_deferred_command_list = native_command_list.get();
            }
            return;
         }
      }
      ASSERT_ONCE(false); // Invalid case?
#endif // ENABLE_NGX
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Dishonored 2\" is developed by Pumbo and Musa and is open source and free.\nIf you enjoy it, consider donating.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         //system("start https://ko-fi.com/realpumbo");
         system("start https://buymeacoffee.com/realfiloppi");
      }
      ImGui::SameLine();
      static const std::string donation_link_musa = std::string("Buy Ersh a Coffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_musa.c_str()))
      {
         system("start https://ko-fi.com/musaqh");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0 //TODOFT: add nexus link here and below
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
         "\nMusa"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         , "");
   }

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      std::string text;

      ImGui::NewLine();
      ImGui::Text("Camera Jitters: ", "");
      // In NCD space
      // Add padding to make it draw consistently even with a "-" in front of the numbers.
      text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y);
      ImGui::Text(text.c_str(), "");
      // In absolute space
      // These values should be between -1 and 1 (note that X might be flipped)
      text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x * device_data.render_resolution.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y * device_data.render_resolution.y);
      ImGui::Text(text.c_str(), "");

      ImGui::NewLine();
      ImGui::Text("Texture Mip LOD Bias: ", "");
      text = std::to_string(device_data.texture_mip_lod_bias_offset);
      ImGui::Text(text.c_str(), "");

      ImGui::NewLine();
      ImGui::Text("Camera: ", "");
      //TODOFT3: figure out if this is meters or what
      text = "Scene Near: " + std::to_string(cb_per_view_global.cb_globalviewinfos.z) + " Scene Far: " + std::to_string(cb_per_view_global.cb_globalviewinfos.w);
      ImGui::Text(text.c_str(), "");
      float tanHalfFOVX = 1.f / projection_matrix.m00;
      float tanHalfFOVY = 1.f / projection_matrix.m11;
      float FOVX = atan(tanHalfFOVX) * 2.0 * 180 / M_PI;
      float FOVY = atan(tanHalfFOVY) * 2.0 * 180 / M_PI;
      text = "Scene: Hor FOV: " + std::to_string(FOVX) + " Vert FOV: " + std::to_string(FOVY);
      ImGui::Text(text.c_str(), "");
      tanHalfFOVX = 1.f / nearest_projection_matrix.m00;
      tanHalfFOVY = 1.f / nearest_projection_matrix.m11;
      FOVX = atan(tanHalfFOVX) * 2.0 * 180 / M_PI;
      FOVY = atan(tanHalfFOVY) * 2.0 * 180 / M_PI;
   }
#endif
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::GAME_NAME = PROJECT_NAME;
      Globals::DESCRIPTION = "Dishonored 2 + Death of the Outsider Luma mod";
      Globals::WEBSITE = "";
      Globals::VERSION = 1;

      shader_hashes_TAA.compute_shaders.emplace(std::stoul("06BBC941", nullptr, 16)); // DH2
      shader_hashes_TAA.compute_shaders.emplace(std::stoul("8EDF67D9", nullptr, 16)); // DH2 Low quality TAA? // TODO: add an assert on this!
      shader_hashes_TAA.compute_shaders.emplace(std::stoul("9F77B624", nullptr, 16)); // DH DOTO
      shader_hashes_UpscaleSharpen.pixel_shaders.emplace(std::stoul("1A0CD2AE", nullptr, 16)); // DH2 + DH DOTO
      shader_hashes_DownsampleDepth.compute_shaders.emplace(std::stoul("27BD5265", nullptr, 16)); // DH2 + DH DOTO
      shader_hashes_UnprojectDepth.compute_shaders.emplace(std::stoul("223FB9DA", nullptr, 16)); // DH2
      shader_hashes_UnprojectDepth.compute_shaders.emplace(std::stoul("74E15FB8", nullptr, 16)); // DH DOTO
      // All UI pixel shaders (these are all Shader Model 4.0, as opposed to the rest of the rendering using SM5.0)
      shader_hashes_UI.pixel_shaders = {
         std::stoul("6FE8114D", nullptr, 16),
         std::stoul("08F8ECFE", nullptr, 16),
         std::stoul("28E5E21A", nullptr, 16),
         std::stoul("38E853C8", nullptr, 16),
         std::stoul("B9E43380", nullptr, 16),
         std::stoul("BC1D41CE", nullptr, 16),
         std::stoul("CC4FB5BF", nullptr, 16),
         std::stoul("D34DA30E", nullptr, 16),
         std::stoul("E3BB1976", nullptr, 16),
         std::stoul("EE4A38D2", nullptr, 16),
      };

      // 6-13 are seemingly totally unused by Dishonored 2
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      enable_separate_ui_drawing = true;

      enable_swapchain_upgrade = true;
      swapchain_upgrade_type = 1;
      enable_texture_format_upgrades = true;
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

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;

      game = new Dishonored2();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(Dishonored2::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(Dishonored2::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(Dishonored2::OnExecuteSecondaryCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}