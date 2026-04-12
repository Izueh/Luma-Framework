#include <cstdint>
#include <d3d11_1.h>
#define GAME_GRANBLUE_FANTASY_RELINK 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define JITTER_PHASES 8
#define PATCH_SCENE_BUFFER 0
#define ENABLE_POST_DRAW_CALLBACK 1

#include <d3d11.h>
#include "..\..\Core\core.hpp"
#include "includes\cbuffers.h"
#include "includes\common.hpp"
#include "includes\hooks.hpp"
#include "includes\safetyhook.hpp"
#include "includes\common.cpp"
#include "includes\hooks.cpp"

namespace
{
#include "includes\upscale.cpp"
#include "includes\postprocess.cpp"

} // namespace

class GranblueFantasyRelink final : public Game
{
   static GameDeviceDataGBFR& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataGBFR*>(device_data.game);
   }
   static const GameDeviceDataGBFR& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataGBFR*>(device_data.game);
   }

public:
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& /*cmd_list_data*/, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Propagate frame jitter to all custom-shader invocations (PostTAA and others).
      const float resX = device_data.render_resolution.x;
      const float resY = device_data.render_resolution.y;
      if (resX > 0.f && resY > 0.f)
      {
         data.GameData.JitterOffset.x = game_device_data.jitter.x * 2.0f / resX;
         data.GameData.JitterOffset.y = game_device_data.jitter.y * -2.0f / resY;
         data.GameData.PrevJitterOffset.x = game_device_data.prev_jitter.x * 2.0f / resX;
         data.GameData.PrevJitterOffset.y = game_device_data.prev_jitter.y * -2.0f / resY;
      }
      data.GameData.IsTAARunning = IsTAARunningThisFrame() ? 1 : 0;
   }
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 9;
      luma_data_cbuffer_index = 8;

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_AFTER_TAA", '1', true, false, "If set to 1, tonemapping will be applied after TAA", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

#if PATCH_SCENE_BUFFER
      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Patch SceneBuffer"),
         ShaderDefinition{"Luma_GBFR_PatchSceneBuffer", reshade::api::pipeline_subobject_type::compute_shader});
#endif

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Post Tonemap"),
         ShaderDefinition{"Luma_GBFR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene Gamma"),
         ShaderDefinition{"Luma_GBFR_CutsceneGamma", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene ColorGrade"),
         ShaderDefinition{"Luma_GBFR_CutsceneColorGrade", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene Overlay Blend PS"),
         ShaderDefinition{"Luma_GBFR_CutsceneOverlayBlend_PS", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Post SR Encode"),
         ShaderDefinition{"Luma_GBFR_PostSREncode", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR UI Encode"),
         ShaderDefinition{"Luma_GBFR_UIEncode", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Pre SR Encode"),
         ShaderDefinition{"Luma_GBFR_PreSREncode", reshade::api::pipeline_subobject_type::pixel_shader});
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
         LoadConfigs();
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data,
      DeviceData& device_data,
      reshade::api::shader_stage stages,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      bool is_custom_pass,
      bool& updated_cbuffers,
      std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      bool is_taa_running = IsTAARunningThisFrame();
      if (is_taa_running && cb_luma_global_settings.GameSettings.IsTAARunning == 0)
      {
         cb_luma_global_settings.GameSettings.IsTAARunning = 1;
         device_data.cb_luma_global_settings_dirty = true;
      }

      bool tonemap_after_taa = *GetShaderDefineData(char_ptr_crc32("TONEMAP_AFTER_TAA")).compiled_data.GetValue() != '0';

      // Since MotionBlur runs before TAA the game runs a smaller AA to dejitter the motion blur input
      // when we reorder the post process effects this is no longer needed and we can use the antialiased output from TAA/SR.
      if (original_shader_hashes.Contains(shader_hashes_MotionBlurDenoise) && tonemap_after_taa)
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_MotionBlur) && tonemap_after_taa)
      {
         game_device_data.motion_blur_seen = true;
         // Game runs motion blur shader twice with different parameters.
         if (game_device_data.motion_blur_invocation_count < 2)
         {
            const size_t pass_index = game_device_data.motion_blur_invocation_count;
            game_device_data.motion_blur_invocation_count++;
            CaptureMotionBlurReplayState(native_device_context, game_device_data, pass_index);

            if (game_device_data.motion_blur_replay_states[pass_index].valid)
            {
               if (pass_index == 0)
               {
                  game_device_data.motion_blur_first_pass_seen = true;
               }
               else
               {
                  game_device_data.motion_blur_second_pass_seen = true;
                  game_device_data.motion_blur_pending.store(true, std::memory_order_release);
               }
               return DrawOrDispatchOverrideType::Skip;
            }
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_Tonemap) && tonemap_after_taa)
      {
         game_device_data.exposure_texture = nullptr;
         game_device_data.exposure_texture_srv = nullptr;
         game_device_data.bloom_texture_srv = nullptr;

         // Capture the game's AdaptLuminance SRV directly from t2
         {
            ComPtr<ID3D11ShaderResourceView> current_exposure_srv;
            native_device_context->PSGetShaderResources(2, 1, current_exposure_srv.put());
            if (current_exposure_srv)
            {
               game_device_data.exposure_texture_srv = current_exposure_srv;

               // Extract the underlying texture so it can be passed to DLSS as an exposure hint.
               ComPtr<ID3D11Resource> resource;
               game_device_data.exposure_texture_srv->GetResource(resource.put());
               if (resource)
               {
                  resource->QueryInterface(game_device_data.exposure_texture.put());
               }
            }
         }

         // get bloom SRV from t3
         {
            ComPtr<ID3D11ShaderResourceView> current_bloom_srv;
            native_device_context->PSGetShaderResources(3, 1, current_bloom_srv.put());
            if (current_bloom_srv)
            {
               game_device_data.bloom_texture_srv = current_bloom_srv;
            }
         }

         game_device_data.tonemap_draw_pending.store(true, std::memory_order_release);
         game_device_data.tonemap_detected_context.store(native_device_context, std::memory_order_release);

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneGamma) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutscenePostPassReplayState(native_device_context, game_device_data.cutscene_gamma_replay_state);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_gamma_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneColorGrade) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutscenePostPassReplayState(native_device_context, game_device_data.cutscene_color_grade_replay_state);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_color_grade_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneOverlayModulate) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutsceneOverlayModulateReplayState(native_device_context, game_device_data);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_overlay_modulate_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneOverlayBlend) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutsceneOverlayBlendReplayState(native_device_context, game_device_data);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_overlay_blend_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_OutlineCS))
      {
         if (tonemap_after_taa)
         {
            CaptureOutlineReplayState(native_device_context, game_device_data);
            if (game_device_data.outline_replay_state.cs_depth_srv)
            {
               game_device_data.outline_replay_state.cs_depth_srv->GetResource(game_device_data.depth_buffer.put());
            }
            if (game_device_data.outline_replay_state.valid)
            {
               PassThroughToComputeUAV(native_device_context);
               game_device_data.outline_pending.store(true, std::memory_order_release);
               return DrawOrDispatchOverrideType::Skip;
            }
         }

         ComPtr<ID3D11ShaderResourceView> cs_depth_srv;
         native_device_context->CSGetShaderResources(2, 1, cs_depth_srv.put());
         if (cs_depth_srv)
         {
            cs_depth_srv->GetResource(game_device_data.depth_buffer.put());
         }
         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_TAA))
      {
         device_data.taa_detected = true;
#if TEST || DEVELOPMENT
         game_device_data.taa_detected_this_frame = true;
#endif
         DrawOrDispatchOverrideType override_type = DrawOrDispatchOverrideType::None;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            override_type = [](GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data) -> DrawOrDispatchOverrideType
            {
#if PATCH_SCENE_BUFFER
               if (!game_device_data.scene_buffer_info_collected.load(std::memory_order_acquire))
               {
                  device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                  LogExpectedCustomDrawSkipped("SR (TAA path)", "force_reset_sr set: scene_buffer_info not collected yet");
#endif
                  return DrawOrDispatchOverrideType::None;
               }
#endif

               if (!ExtractTAAShaderResources(native_device_context, game_device_data))
               {
                  ASSERT_ONCE_MSG(false, "ExtractTAAShaderResources: t3 (source color) or t23 (motion vectors) SRV not bound");
                  device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                  LogExpectedCustomDrawSkipped("SR (TAA path)", "force_reset_sr set: ExtractTAAShaderResources failed (t3 or t23 not bound)");
#endif
                  return DrawOrDispatchOverrideType::None;
               }

               if (render_scale == 1.f)
               {
                  // Get render targets (TAA writes to RT0 and RT1)
                  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                  ID3D11DepthStencilView* dsv;
                  native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
                  if (rtvs[1] == nullptr)
                  {
                     device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TAA path)", "rtvs[1]=null: TAA RTV1 not bound");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }

                  if (!SetupSROutput(native_device, device_data, game_device_data, rtvs[1]))
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TAA path)", "SetupSROutput failed: TAA output texture QueryInterface or min-resolution check failed");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }

                  if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
                  {
                     const bool pre_sr_ok = DrawNativePreSREncodePass(native_device, native_device_context, device_data, game_device_data);
#if TEST || DEVELOPMENT
                     if (!pre_sr_ok)
                     {
                        std::string reason = "prerequisite missing:";
                        const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
                        const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Pre SR Encode"));
                        if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second)
                           reason += " copy_vs=missing;";
                        if (ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
                           reason += " pre_sr_encode_ps=missing;";
                        if (!game_device_data.sr_source_color.get())
                           reason += " sr_source_color=null;";
                        if (!game_device_data.sr_source_color_srv.get())
                           reason += " sr_source_color_srv=null;";
                        LogExpectedCustomDrawSkipped("PreSREncode (TAA path)", reason);
                     }
#endif
                  }

                  // Split here: partial_command_list captures all draws up to (not including) TAA,
                  // including the hash-replaced tonemap passthrough.
                  native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());

                  if (game_device_data.modifiable_index_vertex_buffer)
                  {
                     D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                     native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                     native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
                  }
                  game_device_data.draw_device_context = native_device_context;
               }
               return DrawOrDispatchOverrideType::Replaced;
            }(game_device_data, native_device, native_device_context, device_data);
         }
         else if (render_scale == 1.f)
         {
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ID3D11DepthStencilView* dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            // Some non-SR paths can bind only RT0. Avoid null dereference in SetupTempTAAOutput.
            if (rtvs[1] == nullptr)
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!SetupTempTAAOutput(native_device, game_device_data, rtvs[1]))
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!game_device_data.taa_temp_output_rtv.get() || !game_device_data.taa_output_texture_rtv.get())
            {
               return DrawOrDispatchOverrideType::None;
            }

            rtvs[1] = game_device_data.taa_temp_output_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            (*original_draw_dispatch_func)();
            rtvs[1] = game_device_data.taa_output_texture_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            game_device_data.draw_device_context = native_device_context;
            native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
            override_type = DrawOrDispatchOverrideType::Replaced;
         }
         else
         {
            // Run TAA normally and handle DLSS in TUP
            return DrawOrDispatchOverrideType::None;
         }

         return override_type;
      }

      if (original_shader_hashes.Contains(shader_hashes_Temporal_Upscale))
      {
         DrawOrDispatchOverrideType override_type = DrawOrDispatchOverrideType::Replaced;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            override_type = [](GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data) -> DrawOrDispatchOverrideType
            {
               ID3D11RenderTargetView* rt;
               native_device_context->OMGetRenderTargets(1, &rt, nullptr);
#if TEST || DEVELOPMENT
               if (rt == nullptr)
               {
                  LogExpectedCustomDrawSkipped("SR (TUP path)", "rt=null: TUP RTV0 not bound; SR inputs will not be prepared");
               }
#endif
               if (rt != nullptr)
               {
                  if (!SetupSROutput(native_device, device_data, game_device_data, rt))
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TUP path)", "SetupSROutput failed: TUP output texture QueryInterface or min-resolution check failed");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }
                  // DrawNativePreSREncodePass reads sr_source_color which is written by TAA.
                  // TAA and TUP record on parallel deferred contexts so sr_source_color is not
                  // guaranteed valid here. Deferred to OnExecuteSecondaryCommandList where
                  // the TAA command list has already executed on the immediate context.
               }
               game_device_data.draw_device_context = native_device_context;
               native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
               if (game_device_data.modifiable_index_vertex_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
               }
               return DrawOrDispatchOverrideType::Replaced;
            }(game_device_data, native_device, native_device_context, device_data);
         }
         else
         {
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ID3D11DepthStencilView* dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            if (rtvs[0] == nullptr)
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!SetupTempTAAOutput(native_device, game_device_data, rtvs[0]))
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!game_device_data.taa_temp_output_rtv.get() || !game_device_data.taa_output_texture_rtv.get())
            {
               return DrawOrDispatchOverrideType::None;
            }

            rtvs[0] = game_device_data.taa_temp_output_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            (*original_draw_dispatch_func)();
            rtvs[0] = game_device_data.taa_output_texture_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            game_device_data.draw_device_context = native_device_context;
            native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
         }
         return override_type;
      }
      return DrawOrDispatchOverrideType::None;
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (native_device_context)
      {
         // This is an ExecuteCommandList callback — a command list is about to be replayed
         // on the immediate context. Patch the ring buffer before the first one runs
         // (Map/Unmap already happened on the immediate context at start of frame).
         ComPtr<ID3D11CommandList> native_command_list;
         {
            ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
            secondary_child->QueryInterface(native_command_list.put());
         }
         if (native_command_list)
         {
#if PATCH_SCENE_BUFFER
            if (!game_device_data.scene_buffer_patched_this_frame.load(std::memory_order_relaxed) &&
                game_device_data.scene_buffer_info_collected.load(std::memory_order_acquire))
            {
               std::set<UINT> offsets_to_patch;
               {
                  std::lock_guard<std::mutex> lock(game_device_data.scene_buffer_bindings_mutex);
                  offsets_to_patch = game_device_data.scene_buffer_offsets_this_frame;
               }
               if (!offsets_to_patch.empty())
               {
                  game_device_data.scene_buffer_patched_this_frame.store(true, std::memory_order_release);
                  ComPtr<ID3D11DeviceContext1> immediate_context1;
                  if (SUCCEEDED(native_device_context->QueryInterface(immediate_context1.put())))
                  {
                     for (UINT offset : offsets_to_patch)
                     {
                        PatchSceneBufferInHook(
                           immediate_context1.get(),
                           game_device_data.pending_scene_buffer,
                           offset,
                           game_device_data.pending_num_constants);
                     }
                  }
               }
            }
#endif
         }
         if (native_command_list.get() == game_device_data.remainder_command_list.load(std::memory_order_acquire) && game_device_data.partial_command_list.get() != nullptr)
         {
            {
               native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
               game_device_data.partial_command_list.reset();

               // Read jitter here, mid-frame, after geometry/camera setup but before DLSS and
               // all custom passes. By the time the partial command list (containing TAA) has
               // been replayed the game must have written the current-frame projection jitter.

               game_device_data.prev_table_jitter = game_device_data.table_jitter;
               TryReadTableJitter(game_device_data.table_jitter);
#if TEST || DEVELOPMENT
               game_device_data.prev_jitter = game_device_data.jitter;
               TryReadCameraJitter(game_device_data.jitter);
#endif
               CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();

               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               draw_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);

               bool tonemap_after_taa = *GetShaderDefineData(char_ptr_crc32("TONEMAP_AFTER_TAA")).compiled_data.GetValue() != '0' && IsTAARunningThisFrame();

               // TUP path: TAA partial_command_list has just executed so sr_source_color is valid.
               // DrawNativePreSREncodePass was intentionally deferred from recording time to here.
               if (render_scale != 1.f && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
               {
                  ID3D11Device* native_device_tup = g_native_device_ptr.load(std::memory_order_acquire);
                  if (native_device_tup)
                  {
                     const bool pre_sr_ok = DrawNativePreSREncodePass(native_device_tup, native_device_context.get(), device_data, game_device_data);
#if TEST || DEVELOPMENT
                     if (!pre_sr_ok)
                     {
                        std::string reason = "prerequisite missing:";
                        const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
                        const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Pre SR Encode"));
                        if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second)
                           reason += " copy_vs=missing;";
                        if (ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
                           reason += " pre_sr_encode_ps=missing;";
                        if (!game_device_data.sr_source_color.get())
                           reason += " sr_source_color=null;";
                        if (!game_device_data.sr_source_color_srv.get())
                           reason += " sr_source_color_srv=null;";
                        LogExpectedCustomDrawSkipped("PreSREncode (TUP path)", reason);
                     }
#endif
                  }
               }

               if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
               {
                  auto* sr_instance_data = device_data.GetSRInstanceData();
                  {
                     SR::SettingsData settings_data;
                     settings_data.output_width = static_cast<uint>(device_data.output_resolution.x);
                     settings_data.output_height = static_cast<uint>(device_data.output_resolution.y);
                     settings_data.render_width = static_cast<uint>(device_data.render_resolution.x);
                     settings_data.render_height = static_cast<uint>(device_data.render_resolution.y);
                     settings_data.dynamic_resolution = false;
                     settings_data.hdr = cb_luma_global_settings.DisplayMode == DisplayModeType::HDR ? true : false;
                     settings_data.auto_exposure = true;
                     settings_data.inverted_depth = false;
                     // Granblue MVs are unjittered (g_ProjectionOffset cancels jitter in the PS)
                     settings_data.mvs_jittered = false;
                     settings_data.mvs_x_scale = -(float)device_data.render_resolution.x;
                     settings_data.mvs_y_scale = -(float)device_data.render_resolution.y;
                     settings_data.render_preset = dlss_render_preset;
                     sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
                  }

                  // Prepare SR draw data
                  {
                     bool reset_sr = device_data.force_reset_sr || game_device_data.output_changed;
                     device_data.force_reset_sr = false;
                     float jitter_x = game_device_data.table_jitter.x;
                     float jitter_y = game_device_data.table_jitter.y;
#if TEST || DEVELOPMENT
                     if (!game_device_data.use_table_jitter_for_dlss)
                     {
                        jitter_x = game_device_data.jitter.x;
                        jitter_y = game_device_data.jitter.y;
                     }
#endif
                     SR::SuperResolutionImpl::DrawData draw_data;
                     draw_data.source_color = game_device_data.sr_source_color.get();
                     draw_data.output_color = game_device_data.sr_output_color.get();
                     draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
                     draw_data.depth_buffer = game_device_data.depth_buffer.get();
                     draw_data.pre_exposure = 0.0f;
                     // Pass the engine's AdaptLuminance texture as an exposure hint to DLSS.
                     // Only set when the tonemap intercept has already captured it this frame.
                     // Note: DLSS expects pInExposureTexture to be a 1×1 texture; if AdaptLuminance
                     // is a different size, DLSS may ignore it — auto_exposure=true provides a fallback.
                     if (tonemap_after_taa)
                     {
                        draw_data.exposure = game_device_data.exposure_texture.get();
                     }
                     // Pass the frame jitter tracked by this module.
                     draw_data.jitter_x = jitter_x;
                     draw_data.jitter_y = jitter_y;
                     draw_data.vert_fov = game_device_data.camera_fov;
                     draw_data.far_plane = game_device_data.camera_far;
                     draw_data.near_plane = game_device_data.camera_near;
                     draw_data.reset = reset_sr;
                     draw_data.render_width = device_data.render_resolution.x;
                     draw_data.render_height = device_data.render_resolution.y;

                     // Execute SR
#if TEST || DEVELOPMENT
                     {
                        std::string pre_draw_issues;
                        if (!draw_data.source_color)
                           pre_draw_issues += " source_color=null;";
                        if (!draw_data.output_color)
                           pre_draw_issues += " output_color=null;";
                        if (!draw_data.motion_vectors)
                           pre_draw_issues += " motion_vectors=null;";
                        if (!draw_data.depth_buffer)
                           pre_draw_issues += " depth_buffer=null;";
                        if (!sr_instance_data)
                           pre_draw_issues += " sr_instance_data=null;";
                        if (!pre_draw_issues.empty())
                        {
                           LogExpectedCustomDrawSkipped("SR", "null inputs before Draw:" + pre_draw_issues);
                        }
                     }
#endif
                     if (sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data))
                     {
                        device_data.has_drawn_sr = true;
                     }
                     else
                     {
                        device_data.has_drawn_sr = false;
                        device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                        {
                           std::string reason = "SR::Draw returned false; draw inputs:";
                           if (!draw_data.source_color)
                              reason += " source_color=null;";
                           if (!draw_data.output_color)
                              reason += " output_color=null;";
                           if (!draw_data.motion_vectors)
                              reason += " motion_vectors=null;";
                           if (!draw_data.depth_buffer)
                              reason += " depth_buffer=null;";
                           if (!sr_instance_data)
                              reason += " sr_instance_data=null;";
                           if (draw_data.render_width <= 0.f || draw_data.render_height <= 0.f)
                              reason += " render_dims_invalid(" + std::to_string(static_cast<int>(draw_data.render_width)) + "x" + std::to_string(static_cast<int>(draw_data.render_height)) + ");";
                           LogExpectedCustomDrawSkipped("SR", reason);
                        }
#endif
                     }
#if DEVELOPMENT
                     // Add trace info for DLSS/FSR execution
                     if (device_data.has_drawn_sr)
                     {
                        const std::shared_lock lock_trace(s_mutex_trace);
                        if (trace_running)
                        {
                           const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                           TraceDrawCallData trace_draw_call_data;
                           trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                           trace_draw_call_data.command_list = native_device_context.get();
                           trace_draw_call_data.custom_name = device_data.sr_type == SR::Type::DLSS
                                                                 ? "DLSS-SR"
                                                                 : "FSR-SR";
                           GetResourceInfo(game_device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                           cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                        }
                     }
#endif
                  }
               }

               // Pipeline chain SRV threaded through late-replay passes (split -> PostSREncode -> MotionBlur -> Tonemap -> Cutscene -> UI)
               ID3D11ShaderResourceView* pipeline_color_srv = GetPostAAColorInputSRV(device_data, game_device_data);

               auto run_chained_color_pass = [&](const char* pass_name, auto&& draw_fn) -> bool
               {
                  if (!pipeline_color_srv)
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped(pass_name, "pipeline_color_srv=null");
#endif
                     return false;
                  }

                  ID3D11ShaderResourceView* next_srv = nullptr;
                  if (!draw_fn(pipeline_color_srv, next_srv))
                  {
                     return false;
                  }

                  if (!next_srv)
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped(pass_name, "draw succeeded but output SRV is null");
#endif
                     return false;
                  }

                  pipeline_color_srv = next_srv;
                  return true;
               };

               // PostSREncode normalizes color space: G→L if no HDR-SR, else L→G if HDR-SR, else skip
               {
                  // const bool hdr_sr = (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR) &&
                  //                     (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.has_drawn_sr);
                  // const bool should_run = tonemap_after_taa ? !hdr_sr : hdr_sr;
                  // if (should_run)
                  {
                     if (pipeline_color_srv)
                     {
                        const bool post_sr_ok = run_chained_color_pass("PostSREncode", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                        {
                           // Use pre-DLSS input for alpha extraction if SR was executed.
                           ID3D11ShaderResourceView* alpha_source_srv = nullptr;
                           if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.has_drawn_sr)
                           {
                              alpha_source_srv = game_device_data.sr_source_color_srv.get();
                           }

                           if (!DrawNativePostSREncodePass(native_device_context.get(), cmd_list_data, device_data, game_device_data, input_srv, alpha_source_srv))
                           {
                              return false;
                           }

                           out_srv = game_device_data.post_sr_encode_srv.get();
                           return true;
                        });
#if TEST || DEVELOPMENT
                        if (!post_sr_ok)
                        {
                           std::string reason = "DrawNativePostSREncodePass failed:";
                           {
                              const auto vs_chk = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
                              if (vs_chk == device_data.native_vertex_shaders.end() || !vs_chk->second)
                                 reason += " copy_vs=missing;";
                           }
                           {
                              const auto ps_chk = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Post SR Encode"));
                              if (ps_chk == device_data.native_pixel_shaders.end() || !ps_chk->second)
                                 reason += " post_sr_encode_ps=missing;";
                           }
                           if (!pipeline_color_srv)
                              reason += " input_srv=null;";
                           LogExpectedCustomDrawSkipped("PostSREncode", reason);
                        }
#endif
                     }
#if TEST || DEVELOPMENT
                     else
                     {
                        LogExpectedCustomDrawSkipped("PostSREncode", "should_run=true but pipeline_color_srv is null");
                     }
#endif
                  }
               }

               if (game_device_data.motion_blur_pending.load(std::memory_order_acquire))
               {
                  const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
                  const bool after_sr = !use_sr_input || device_data.has_drawn_sr;
                  if (after_sr && CanDrawNativeMotionBlurPass(pipeline_color_srv, game_device_data))
                  {
                     const bool motion_blur_ok = run_chained_color_pass("MotionBlur", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeMotionBlurPass(native_device_context.get(), cmd_list_data, device_data, game_device_data, input_srv);
                        if (!game_device_data.motion_blur_output_ready)
                        {
                           return false;
                        }

                        out_srv = game_device_data.motion_blur_output_srv.get();
                        return true;
                     });
#if TEST || DEVELOPMENT
                     if (!motion_blur_ok)
                     {
                        LogExpectedCustomDrawSkipped("MotionBlur", "DrawNativeMotionBlurPass did not produce output (motion_blur_output_ready=false)");
                     }
#endif
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason;
                     if (!after_sr)
                     {
                        reason = "pending replay, but SR output is not ready yet";
                     }
                     else
                     {
                        reason = "prerequisite missing:";
                        if (!pipeline_color_srv)
                           reason += " pipeline_color_srv=null;";
                        if (!game_device_data.motion_blur_first_pass_seen)
                           reason += " first_pass_not_seen;";
                        if (!game_device_data.motion_blur_second_pass_seen)
                           reason += " second_pass_not_seen;";
                        if (!game_device_data.motion_blur_replay_states[0].valid)
                           reason += " replay_state0_invalid;";
                        if (!game_device_data.motion_blur_replay_states[1].valid)
                           reason += " replay_state1_invalid;";
                        if (!game_device_data.taa_output_texture.get())
                           reason += " taa_output_texture=null;";
                     }
                     LogExpectedCustomDrawSkipped("MotionBlur", reason);
                  }
#endif
                  // Never stall tonemap if motion blur replay is unavailable in this frame.
                  game_device_data.motion_blur_pending.store(false, std::memory_order_release);
                  game_device_data.motion_blur_seen = false;
                  game_device_data.motion_blur_first_pass_seen = false;
                  game_device_data.motion_blur_second_pass_seen = false;
                  game_device_data.motion_blur_output_ready = false;
                  game_device_data.motion_blur_invocation_count = 0;
                  game_device_data.motion_blur_replay_states[0].Reset();
                  game_device_data.motion_blur_replay_states[1].Reset();
               }

               // Tonemap writes to cutscene_intermediate_rtv for UIEncode consumption
               if (game_device_data.tonemap_draw_pending.load(std::memory_order_acquire))
               {
                  const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
                  const bool after_sr = !use_sr_input || device_data.has_drawn_sr;
                  const bool ready = after_sr && CanDrawNativeTonemapPass(pipeline_color_srv, game_device_data);
                  if (ready)
                  {
                     const bool tonemap_ok = run_chained_color_pass("Tonemap", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeTonemapPass(native_device_context.get(), cmd_list_data, device_data, game_device_data, input_srv);
                        out_srv = game_device_data.cutscene_intermediate_srv.get();
                        return true;
                     });
                     game_device_data.tonemap_draw_pending.store(false, std::memory_order_release);
                     game_device_data.exposure_texture = nullptr;
                     game_device_data.exposure_texture_srv = nullptr;
                     game_device_data.bloom_texture_srv = nullptr;
#if TEST || DEVELOPMENT
                     if (!tonemap_ok)
                     {
                        LogExpectedCustomDrawSkipped("Tonemap", "DrawNativeTonemapPass did not produce output SRV");
                     }
#endif
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason;
                     if (!after_sr)
                     {
                        reason = "pending replay, but SR output is not ready yet";
                     }
                     else
                     {
                        reason = "prerequisite missing:";
                        if (!pipeline_color_srv)
                           reason += " pipeline_color_srv=null;";
                        if (!game_device_data.exposure_texture_srv.get())
                           reason += " exposure_texture_srv=null;";
                        if (!game_device_data.bloom_texture_srv.get())
                           reason += " bloom_texture_srv=null;";
                        if (!game_device_data.taa_output_texture.get())
                           reason += " taa_output_texture=null;";
                     }
                     LogExpectedCustomDrawSkipped("Tonemap", reason);
                  }
#endif
               }

               if (game_device_data.cutscene_gamma_pending.load(std::memory_order_acquire))
               {
                  if (CanDrawNativeCutsceneGammaPass(game_device_data))
                  {
                     run_chained_color_pass("CutsceneGamma", [&](ID3D11ShaderResourceView* /*input_srv*/, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeCutsceneGammaPass(native_device_context.get(), cmd_list_data, device_data, game_device_data);
                        out_srv = game_device_data.cutscene_gamma_srv.get();
                        return true;
                     });
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason = "prerequisite missing:";
                     if (!game_device_data.cutscene_intermediate_srv.get())
                        reason += " cutscene_intermediate_srv=null;";
                     if (!game_device_data.taa_output_texture.get())
                        reason += " taa_output_texture=null;";
                     if (!game_device_data.cutscene_gamma_replay_state.valid)
                        reason += " cutscene_gamma_replay_state_invalid;";
                     LogExpectedCustomDrawSkipped("CutsceneGamma", reason);
                  }
#endif
                  game_device_data.cutscene_gamma_pending.store(false, std::memory_order_release);
                  game_device_data.cutscene_gamma_replay_state.Reset();
               }

               if (game_device_data.cutscene_color_grade_pending.load(std::memory_order_acquire))
               {
                  if (CanDrawNativeCutsceneColorGradePass(game_device_data))
                  {
                     run_chained_color_pass("CutsceneColorGrade", [&](ID3D11ShaderResourceView* /*input_srv*/, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeCutsceneColorGradePass(native_device_context.get(), cmd_list_data, device_data, game_device_data);
                        out_srv = game_device_data.cutscene_color_grade_srv.get();
                        return true;
                     });
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason = "prerequisite missing:";
                     if (!game_device_data.cutscene_gamma_srv.get())
                        reason += " cutscene_gamma_srv=null;";
                     if (!game_device_data.taa_output_texture.get())
                        reason += " taa_output_texture=null;";
                     if (!game_device_data.cutscene_color_grade_replay_state.valid)
                        reason += " cutscene_color_grade_replay_state_invalid;";
                     LogExpectedCustomDrawSkipped("CutsceneColorGrade", reason);
                  }
#endif
                  game_device_data.cutscene_color_grade_pending.store(false, std::memory_order_release);
                  game_device_data.cutscene_color_grade_replay_state.Reset();
               }

               if (game_device_data.cutscene_overlay_modulate_pending.load(std::memory_order_acquire))
               {
                  if (CanDrawNativeCutsceneOverlayModulatePass(pipeline_color_srv, game_device_data))
                  {
                     const bool overlay_modulate_ok = run_chained_color_pass("CutsceneOverlayModulate", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeCutsceneOverlayModulatePass(native_device_context.get(), cmd_list_data, device_data, game_device_data, input_srv);
                        out_srv = game_device_data.cutscene_overlay_modulate_srv.get();
                        return true;
                     });
#if TEST || DEVELOPMENT
                     if (!overlay_modulate_ok)
                     {
                        LogExpectedCustomDrawSkipped("CutsceneOverlayModulate", "DrawNativeCutsceneOverlayModulatePass did not produce output SRV");
                     }
#endif
                     game_device_data.cutscene_overlay_modulate_pending.store(false, std::memory_order_release);
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason = "prerequisite missing:";
                     if (!pipeline_color_srv)
                        reason += " pipeline_color_srv=null;";
                     if (!game_device_data.taa_output_texture.get())
                        reason += " taa_output_texture=null;";
                     if (!game_device_data.cutscene_overlay_modulate_replay_state.valid)
                        reason += " cutscene_overlay_modulate_replay_state_invalid;";
                     LogExpectedCustomDrawSkipped("CutsceneOverlayModulate", reason);
                  }
#endif
                  game_device_data.cutscene_overlay_modulate_replay_state.Reset();
               }

               if (game_device_data.cutscene_overlay_blend_pending.load(std::memory_order_acquire))
               {
                  if (CanDrawNativeCutsceneOverlayBlendPass(pipeline_color_srv, game_device_data))
                  {
                     run_chained_color_pass("CutsceneOverlayBlend", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        DrawNativeCutsceneOverlayBlendPass(native_device_context.get(), cmd_list_data, device_data, game_device_data, input_srv, game_device_data.cutscene_intermediate_rtv.get());
                        out_srv = game_device_data.cutscene_intermediate_srv.get();
                        return true;
                     });
                     game_device_data.cutscene_overlay_blend_pending.store(false, std::memory_order_release);
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason = "prerequisite missing:";
                     if (!pipeline_color_srv)
                        reason += " pipeline_color_srv=null;";
                     if (!game_device_data.cutscene_intermediate_rtv.get())
                        reason += " cutscene_intermediate_rtv=null;";
                     if (!game_device_data.taa_output_texture.get())
                        reason += " taa_output_texture=null;";
                     if (!game_device_data.cutscene_overlay_blend_replay_state.valid)
                        reason += " cutscene_overlay_blend_replay_state_invalid;";
                     LogExpectedCustomDrawSkipped("CutsceneOverlayBlend", reason);
                  }
#endif

                  game_device_data.cutscene_overlay_blend_replay_state.Reset();
               }

               if (game_device_data.outline_pending.load(std::memory_order_acquire))
               {
                  if (CanDrawNativeOutlinePass(pipeline_color_srv, game_device_data))
                  {
                     const bool outline_ok = run_chained_color_pass("OutlineCS", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
                     {
                        if (!DrawNativeOutlinePass(native_device_context.get(), device_data, game_device_data, input_srv))
                        {
                           return false;
                        }

                        out_srv = game_device_data.outline_srv.get();
                        return true;
                     });
#if TEST || DEVELOPMENT
                     if (!outline_ok)
                     {
                        LogExpectedCustomDrawSkipped("OutlineCS", "DrawNativeOutlinePass failed");
                     }
#endif
                  }
#if TEST || DEVELOPMENT
                  else
                  {
                     std::string reason = "prerequisite missing:";
                     if (!pipeline_color_srv)
                        reason += " pipeline_color_srv=null;";
                     if (!game_device_data.outline_replay_state.valid)
                        reason += " outline_replay_state_invalid;";
                     if (!game_device_data.taa_output_texture.get())
                        reason += " taa_output_texture=null;";
                     LogExpectedCustomDrawSkipped("OutlineCS", reason);
                  }
#endif
                  game_device_data.outline_pending.store(false, std::memory_order_release);
                  game_device_data.outline_replay_state.Reset();
               }

               {
                  bool can_draw_ui_encode = CanDrawNativeUIEncodePass(pipeline_color_srv, game_device_data);
                  const bool sr_enabled = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#if TEST || DEVELOPMENT
                  if (sr_enabled)
                  {
                     if (!game_device_data.pre_sr_encode_srv.get() || !device_data.has_drawn_sr)
                     {
                        std::string reason = "SR enabled but prerequisite missing:";
                        if (!game_device_data.pre_sr_encode_srv.get())
                           reason += " pre_sr_encode_srv=null;";
                        if (!device_data.has_drawn_sr)
                           reason += " sr_not_drawn;";
                        LogExpectedCustomDrawSkipped("UIEncode (SR composite inputs)", reason);
                     }
                  }
#endif

#if TEST || DEVELOPMENT
                  if (!can_draw_ui_encode)
                  {
                     std::string reason = "prerequisite missing:";
                     if (!pipeline_color_srv)
                        reason += " pipeline_color_srv=null;";
                     if (!game_device_data.taa_output_texture_rtv.get())
                        reason += " taa_output_texture_rtv=null;";
                     LogExpectedCustomDrawSkipped("UIEncode", reason);
                  }
#endif

                  if (can_draw_ui_encode)
                  {
                     DrawNativeUIEncodePass(native_device_context.get(), cmd_list_data, device_data, game_device_data, pipeline_color_srv);
                  }
               }

               draw_state_stack.Restore(native_device_context.get());
               compute_state_stack.Restore(native_device_context.get());
            }
         }
      }

      ComPtr<ID3D11CommandList> finish_command_list;
      hr = device_child->QueryInterface(finish_command_list.put());
      if (finish_command_list)
      {
         ComPtr<ID3D11DeviceContext> deferred_ctx;
         ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = secondary_child->QueryInterface(deferred_ctx.put());
         if (deferred_ctx)
         {
            if (deferred_ctx.get() == game_device_data.draw_device_context)
            {
               game_device_data.remainder_command_list.store(finish_command_list.get(), std::memory_order_release);
            }
         }
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if PATCH_SCENE_BUFFER
      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = CBSceneBuffer_size;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = CBSceneBuffer_size;
         HRESULT hr = native_device->CreateBuffer(&bd, nullptr, game_device_data.scratch_scene_buffer.put());
         ASSERT_ONCE(SUCCEEDED(hr));
      }

      if (game_device_data.scratch_scene_buffer)
      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
         uavd.Format = DXGI_FORMAT_UNKNOWN; // Required for structured buffers
         uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         uavd.Buffer.FirstElement = 0;
         uavd.Buffer.Flags = 0;
         uavd.Buffer.NumElements = 1;
         HRESULT hr = native_device->CreateUnorderedAccessView(
            game_device_data.scratch_scene_buffer.get(), &uavd, game_device_data.scratch_scene_buffer_uav.put());
         ASSERT_ONCE(SUCCEEDED(hr));
      }
#endif
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataGBFR;

      {
         auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data.game);
         game_device_data.jitter = float2{0.0f, 0.0f};
         game_device_data.prev_jitter = game_device_data.jitter;
      }

      g_device_data_ptr.store(&device_data, std::memory_order_release);
      g_native_device_ptr.store(native_device, std::memory_order_release);

      if (!g_rt_creation_hook)
      {
         const uintptr_t base_addr = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
         if (base_addr != 0)
         {
            void* rt_creation_fn = reinterpret_cast<void*>(base_addr + kInitializeDX11RenderingPipeline_RVA);
            g_rt_creation_hook = safetyhook::create_inline(
               rt_creation_fn,
               reinterpret_cast<void*>(&Hooked_InitializeDX11RenderingPipeline));
         }
      }

      if (!g_update_screen_resolution_hook)
      {
         const uintptr_t base_addr = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
         if (base_addr != 0)
         {
            void* update_res_fn = reinterpret_cast<void*>(base_addr + kUpdateScreenResolution_RVA);
            g_update_screen_resolution_hook = safetyhook::create_inline(
               update_res_fn,
               reinterpret_cast<void*>(&Hooked_UpdateScreenResolution));
         }
      }

      PatchJitterPhases();

      if (!g_jitter_write_hook)
      {
         const uintptr_t base_addr = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
         if (base_addr != 0)
         {
            g_jitter_write_hook = safetyhook::create_mid(
               reinterpret_cast<void*>(base_addr + kJitterWrite_RVA),
               &OnJitterWrite);
         }
      }

#if PATCH_SCENE_BUFFER
      ComPtr<ID3D11DeviceContext> immediate_context;
      native_device->GetImmediateContext(immediate_context.put());

      ComPtr<ID3D11DeviceContext1> immediate_context1;
      if (SUCCEEDED(immediate_context->QueryInterface(immediate_context1.put())))
      {
         void* immediate_fn = GetVTableFunction(immediate_context1.get(), kVSSetConstantBuffers1_VTableIndex);

         if (!g_VSSetConstantBuffers1_hook_immediate)
         {
            g_VSSetConstantBuffers1_hook_immediate = safetyhook::create_inline(
               immediate_fn,
               reinterpret_cast<void*>(&Hooked_VSSetConstantBuffers1_Immediate));
         }

         // Check if the deferred context uses a different function implementation;
         // if so, install a second inline hook for it.
         ComPtr<ID3D11DeviceContext> deferred_context;
         if (SUCCEEDED(native_device->CreateDeferredContext(0, deferred_context.put())))
         {
            ComPtr<ID3D11DeviceContext1> deferred_context1;
            if (SUCCEEDED(deferred_context->QueryInterface(deferred_context1.put())))
            {
               void* deferred_fn = GetVTableFunction(deferred_context1.get(), kVSSetConstantBuffers1_VTableIndex);

               if (deferred_fn != immediate_fn && !g_VSSetConstantBuffers1_hook_deferred)
               {
                  g_VSSetConstantBuffers1_hook_deferred = safetyhook::create_inline(
                     deferred_fn,
                     reinterpret_cast<void*>(&Hooked_VSSetConstantBuffers1_Deferred));
               }
            }
         }
      }
#endif
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT
      {
         const bool was_down = game_device_data.pause_trace_key_down;
         game_device_data.pause_trace_key_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
         if (game_device_data.pause_trace_key_down && !was_down)
         {
            game_device_data.pause_trace_delay_countdown = game_device_data.pause_trace_delay_frames;
         }
         if (game_device_data.pause_trace_delay_countdown >= 0)
         {
            if (game_device_data.pause_trace_delay_countdown == 0)
            {
               trace_scheduled = true;
               game_device_data.pause_trace_delay_countdown = -1;

               const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
               const uintptr_t settings_obj = (mod_base != 0)
                                                 ? *reinterpret_cast<const uintptr_t*>(mod_base + kTAASettingsGlobal_RVA)
                                                 : 0;

               auto& snap = game_device_data.pause_snapshot;
               snap.valid = true;
               snap.render_resolution = device_data.render_resolution;
               snap.output_resolution = device_data.output_resolution;
               snap.render_scale_pct = render_scale * 100.0f;
               snap.jitter = game_device_data.jitter;
               snap.prev_jitter = game_device_data.prev_jitter;
               snap.prev_table_jitter = game_device_data.prev_table_jitter;
               snap.table_jitter = game_device_data.table_jitter;
               snap.taa_enabled = IsTAARunningThisFrame();
               snap.settings_obj_valid = (settings_obj != 0);
               snap.upscaling_disabled = (settings_obj != 0) && ((*reinterpret_cast<const uint8_t*>(settings_obj + 63) & 1) != 0);
               snap.drs_active = (settings_obj != 0) && ((*reinterpret_cast<const uint8_t*>(settings_obj + 101) & 1) != 0);
               snap.taa_output_ready = game_device_data.taa_output_texture.get() != nullptr;
            }
            else
            {
               --game_device_data.pause_trace_delay_countdown;
            }
         }
      }
#endif

      if (!device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && game_device_data.taa_detected_this_frame)
         {
            reshade::log::message(reshade::log::level::warning,
               "[GBFR][TEST] SR not drawn this frame (sr enabled, not suppressed, TAA was seen); force_reset_sr set");
         }
#endif
      }
      device_data.has_drawn_sr = false;
      game_device_data.tonemap_detected_context.store(nullptr, std::memory_order_relaxed);
#if TEST || DEVELOPMENT
      game_device_data.taa_detected_this_frame = false;
#endif
      game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
      game_device_data.draw_device_context = nullptr;
      game_device_data.sr_source_color = nullptr;
      game_device_data.sr_source_color_srv = nullptr;
      game_device_data.pre_sr_encode_texture = nullptr;
      game_device_data.pre_sr_encode_srv = nullptr;
      game_device_data.pre_sr_encode_rtv = nullptr;
      game_device_data.outline_pending.store(false, std::memory_order_relaxed);
      game_device_data.outline_replay_state.Reset();
      game_device_data.depth_buffer = nullptr;
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.bloom_texture_srv = nullptr;
      game_device_data.exposure_texture = nullptr;
      game_device_data.exposure_texture_srv = nullptr;
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_output_texture_rtv = nullptr;
      game_device_data.motion_blur_replay_states[0].Reset();
      game_device_data.motion_blur_replay_states[1].Reset();
      game_device_data.motion_blur_pending.store(false, std::memory_order_relaxed);
      game_device_data.motion_blur_seen = false;
      game_device_data.motion_blur_first_pass_seen = false;
      game_device_data.motion_blur_second_pass_seen = false;
      game_device_data.motion_blur_output_ready = false;
      game_device_data.motion_blur_invocation_count = 0;
      game_device_data.cutscene_gamma_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_color_grade_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_gamma_resource = nullptr;
      game_device_data.cutscene_gamma_srv = nullptr;
      game_device_data.cutscene_gamma_rtv = nullptr;
      game_device_data.cutscene_color_grade_resource = nullptr;
      game_device_data.cutscene_color_grade_srv = nullptr;
      game_device_data.cutscene_color_grade_rtv = nullptr;
      game_device_data.cutscene_overlay_blend_replay_state.Reset();
      game_device_data.cutscene_overlay_modulate_replay_state.Reset();
      game_device_data.cutscene_gamma_replay_state.Reset();
      game_device_data.cutscene_color_grade_replay_state.Reset();
      game_device_data.tonemap_draw_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_overlay_blend_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_overlay_modulate_pending.store(false, std::memory_order_relaxed);
      // cutscene_intermediate_* is a persistent resource; not reset per-frame.
      game_device_data.scene_buffer_patched_this_frame = false;
      game_device_data.scene_buffer_collect_guard.store(false, std::memory_order_relaxed);
      game_device_data.scene_buffer_info_collected.store(false, std::memory_order_relaxed);
      game_device_data.pending_scene_buffer = nullptr;
      game_device_data.pending_first_constant = 0;
      game_device_data.pending_num_constants = 0;
      {
         std::lock_guard<std::mutex> lock(game_device_data.scene_buffer_bindings_mutex);
         game_device_data.scene_buffer_offsets_this_frame.clear();
      }

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }

      if (render_scale_changed)
      {
         device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
         reshade::log::message(reshade::log::level::warning, "[GBFR][TEST] force_reset_sr set: render_scale_changed");
#endif
         render_scale_changed = false;
      }
      device_data.cb_luma_global_settings_dirty = true;
      int32_t sr_type = static_cast<int32_t>(device_data.sr_type);
      cb_luma_global_settings.SRType = static_cast<uint32_t>(sr_type + 1);
      cb_luma_global_settings.GameSettings.IsTAARunning = 0;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "RenderScale", render_scale);
      if (render_scale != 1.f)
      {
         render_scale_changed = true;
      }
      // Load cbuffer values directly from config
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      reshade::get_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
      reshade::get_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      reshade::get_config_value(runtime, NAME, "ContrastShadows", cb_luma_global_settings.GameSettings.ShadowContrast);
      reshade::get_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
      reshade::get_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
      reshade::get_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
      reshade::get_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      reshade::get_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      reshade::api::effect_runtime* runtime = nullptr;

      // Render scale slider
      {
         int scale = static_cast<int>(render_scale * 100.0f);
         if (ImGui::SliderInt("Render Scale (%)", &scale, 50, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
         {
            scale = (scale / 5) * 5;
            render_scale = scale / 100.0f;
            render_scale_changed = true;
            reshade::set_config_value(runtime, NAME, "RenderScale", render_scale);
         }
      }

      if (ImGui::TreeNodeEx("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
      {

         float contrast = cb_luma_global_settings.GameSettings.Contrast * 50.0f;
         float highlights = cb_luma_global_settings.GameSettings.Highlights * 50.0f;
         float highlight_contrast = cb_luma_global_settings.GameSettings.HighlightContrast * 50.0f;
         float shadows = cb_luma_global_settings.GameSettings.Shadows * 50.0f;
         float shadow_contrast = cb_luma_global_settings.GameSettings.ShadowContrast * 50.0f;
         float flare = cb_luma_global_settings.GameSettings.Flare * 100.0f;
         float saturation = cb_luma_global_settings.GameSettings.Saturation * 50.0f;
         float dechroma = cb_luma_global_settings.GameSettings.Dechroma * 100.0f;
         float highlight_saturation = cb_luma_global_settings.GameSettings.HighlightSaturation * 50.0f;
         int bloom_type = cb_luma_global_settings.GameSettings.BloomType;
         float blooom_strength = cb_luma_global_settings.GameSettings.BloomStrength * 50.0f;

         if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, 1.f, "Exposure", runtime))
         {
            cb_luma_global_settings.GameSettings.Exposure = 1.f;
            reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
         }

         if (ImGui::SliderFloat("Gamma", &cb_luma_global_settings.GameSettings.Gamma, 0.75f, 1.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.Gamma, 1.f, "Gamma", runtime))
         {
            cb_luma_global_settings.GameSettings.Gamma = 1.f;
            reshade::set_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
         }

         if (ImGui::SliderFloat("Highlights", &highlights, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Highlights = highlights * 0.02f;
            reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
         }
         if (DrawResetButton(highlights, 50.f, "Highlights", runtime))
         {
            highlights = 50.f;
            cb_luma_global_settings.GameSettings.Highlights = highlights * 0.02f;
            reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
         }

         if (ImGui::SliderFloat("Highlight Contrast", &highlight_contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.HighlightContrast = highlight_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
         }
         if (DrawResetButton(highlight_contrast, 50.f, "HighlightContrast", runtime))
         {
            highlight_contrast = 50.f;
            cb_luma_global_settings.GameSettings.HighlightContrast = highlight_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
         }

         if (ImGui::SliderFloat("Shadows", &shadows, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Shadows = shadows * 0.02f;
            reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
         }
         if (DrawResetButton(shadows, 50.f, "Shadows", runtime))
         {
            shadows = 50.f;
            cb_luma_global_settings.GameSettings.Shadows = shadows * 0.02f;
            reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
         }

         if (ImGui::SliderFloat("Shadow Contrast", &shadow_contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.ShadowContrast = shadow_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "ShadowContrast", cb_luma_global_settings.GameSettings.ShadowContrast);
         }
         if (DrawResetButton(shadow_contrast, 50.f, "ShadowContrast", runtime))
         {
            shadow_contrast = 50.f;
            cb_luma_global_settings.GameSettings.ShadowContrast = shadow_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "ShadowContrast", cb_luma_global_settings.GameSettings.ShadowContrast);
         }

         if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Contrast = contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
         }
         if (DrawResetButton(contrast, 50.f, "Contrast", runtime))
         {
            contrast = 50.f;
            cb_luma_global_settings.GameSettings.Contrast = contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
         }

         if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Saturation = saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
         }
         if (DrawResetButton(saturation, 50.f, "Saturation", runtime))
         {
            saturation = 50.f;
            cb_luma_global_settings.GameSettings.Saturation = saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
         }

         if (ImGui::SliderFloat("Highlight Saturation", &highlight_saturation, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.HighlightSaturation = highlight_saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
         }
         if (DrawResetButton(highlight_saturation, 50.f, "HighlightSaturation", runtime))
         {
            highlight_saturation = 50.f;
            cb_luma_global_settings.GameSettings.HighlightSaturation = highlight_saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
         }

         if (ImGui::SliderFloat("Dechroma", &dechroma, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Dechroma = dechroma * 0.01f;
            reshade::set_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
         }
         if (DrawResetButton(dechroma, 0.f, "Dechroma", runtime))
         {
            dechroma = 0.f;
            cb_luma_global_settings.GameSettings.Dechroma = dechroma * 0.01f;
            reshade::set_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
         }

         if (ImGui::SliderFloat("Flare", &flare, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Flare = flare * 0.01f;
            reshade::set_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
         }
         if (DrawResetButton(flare, 0.f, "Flare", runtime))
         {
            flare = 0.f;
            cb_luma_global_settings.GameSettings.Flare = flare * 0.01f;
            reshade::set_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
         }

         // Bloom settings
         const char* bloom_type_names[] = {"Vanilla", "HDR"};
         if (ImGui::SliderInt("Bloom Type", &bloom_type, 0, 1, bloom_type_names[bloom_type], ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.BloomType = bloom_type;
            reshade::set_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
         }
         if (DrawResetButton(bloom_type, 1, "BloomType", runtime))
         {
            bloom_type = 1;
            cb_luma_global_settings.GameSettings.BloomType = bloom_type;
            reshade::set_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
         }

         if (ImGui::SliderFloat("Bloom Strength", &blooom_strength, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.BloomStrength = blooom_strength * 0.02f;
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         }
         if (DrawResetButton(blooom_strength, 100.f, "BloomStrength", runtime))
         {
            blooom_strength = 50.f;
            cb_luma_global_settings.GameSettings.BloomStrength = blooom_strength * 0.02f;
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         }

         ImGui::TreePop();
      }
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::SliderInt("Pause Trace Delay (frames)", &game_device_data.pause_trace_delay_frames, 0, 10);
      ImGui::Checkbox("Use Table Jitter for DLSS", &game_device_data.use_table_jitter_for_dlss);
   }
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));

      // Read TAA settings object for per-bit queries beyond the TAA-enabled flag
      const uintptr_t settings_obj = (mod_base != 0)
                                        ? *reinterpret_cast<const uintptr_t*>(mod_base + kTAASettingsGlobal_RVA)
                                        : 0;

      ImGui::NewLine();
      if (ImGui::BeginTable("gbfr_info", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
      {
         ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableHeadersRow();

         // Resolution
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Render Resolution");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%dx%d", (int)device_data.render_resolution.x, (int)device_data.render_resolution.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Output Resolution");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%dx%d", (int)device_data.output_resolution.x, (int)device_data.output_resolution.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Render Scale");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.0f%%", render_scale * 100.0f);

         // Camera jitter
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Jitter (NDC)");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.6f, %.6f", game_device_data.jitter.x, game_device_data.jitter.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Prev Jitter (NDC)");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.6f, %.6f", game_device_data.prev_jitter.x, game_device_data.prev_jitter.y);

         // Jitter phase and direct table read
         {
            const uint8_t phase = (mod_base != 0)
                                     ? (*reinterpret_cast<const uint8_t*>(mod_base + kJitterPhaseCounter_RVA) & static_cast<uint8_t>(JITTER_PHASES - 1))
                                     : 0u;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter Phase");
            ImGui::TableSetColumnIndex(1);
            if (mod_base != 0)
               ImGui::Text("%u / %u", static_cast<unsigned>(phase), static_cast<unsigned>(JITTER_PHASES));
            else
               ImGui::TextUnformatted("N/A");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", game_device_data.table_jitter.x, game_device_data.table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", game_device_data.prev_table_jitter.x, game_device_data.prev_table_jitter.y);

#if DEVELOPMENT
            {
               {
                  ASSERT_ONCE_MSG(
                     fabsf(game_device_data.table_jitter.x - game_device_data.jitter.x) < 1e-4f &&
                        fabsf(game_device_data.table_jitter.y - game_device_data.jitter.y) < 1e-4f,
                     "Jitter table value does not match camera projection jitter");
               }
            }
#endif
         }

         // TAA state (read from engine globals at runtime)
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("TAA Enabled");
         ImGui::TableSetColumnIndex(1);
         ImGui::TextUnformatted(IsTAARunningThisFrame() ? "Yes" : "No");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Upscaling (TUP)");
         ImGui::TableSetColumnIndex(1);
         if (settings_obj != 0)
            ImGui::TextUnformatted((*reinterpret_cast<const uint8_t*>(settings_obj + 63) & 1) ? "Disabled" : "Enabled");
         else
            ImGui::TextUnformatted("N/A");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("DRS Active");
         ImGui::TableSetColumnIndex(1);
         if (settings_obj != 0)
            ImGui::TextUnformatted((*reinterpret_cast<const uint8_t*>(settings_obj + 101) & 1) ? "Yes" : "No");
         else
            ImGui::TextUnformatted("N/A");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("TAA Output Texture");
         ImGui::TableSetColumnIndex(1);
         ImGui::TextUnformatted(game_device_data.taa_output_texture.get() ? "Ready" : "Null");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Likely Pause Branch: byte_1461720A4 bit0");
         ImGui::TableSetColumnIndex(1);
         if (mod_base != 0)
         {
            const uint8_t v = *reinterpret_cast<const uint8_t*>(mod_base + kPauseCandidate_GlobalBit_RVA);
            ImGui::Text("%s (0x%02X)", (v & 1) ? "1/true" : "0/false", static_cast<unsigned>(v));
         }
         else
         {
            ImGui::TextUnformatted("N/A");
         }

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Candidate: byte_145E5CABD (Tonemap gate)");
         ImGui::TableSetColumnIndex(1);
         if (mod_base != 0)
         {
            const uint8_t v = *reinterpret_cast<const uint8_t*>(mod_base + kPauseCandidate_TonemapGate_RVA);
            ImGui::Text("%s (0x%02X)", (v == 1) ? "1/enabled" : "!=1/disabled", static_cast<unsigned>(v));
         }
         else
         {
            ImGui::TextUnformatted("N/A");
         }

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Candidate: byte_146130C5C (DoF/Bloom)");
         ImGui::TableSetColumnIndex(1);
         if (mod_base != 0)
         {
            const uint8_t v = *reinterpret_cast<const uint8_t*>(mod_base + kPauseCandidate_DofGateA_RVA);
            ImGui::Text("%s (0x%02X)", (v == 0) ? "0/branch-taken" : "!=0/branch-skipped", static_cast<unsigned>(v));
         }
         else
         {
            ImGui::TextUnformatted("N/A");
         }

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Candidate: byte_146130E13 (DoF/Bloom)");
         ImGui::TableSetColumnIndex(1);
         if (mod_base != 0)
         {
            const uint8_t v = *reinterpret_cast<const uint8_t*>(mod_base + kPauseCandidate_DofGateB_RVA);
            ImGui::Text("%s (0x%02X)", (v == 0) ? "0/branch-taken" : "!=0/branch-skipped", static_cast<unsigned>(v));
         }
         else
         {
            ImGui::TextUnformatted("N/A");
         }

         ImGui::EndTable();
      }
#if DEVELOPMENT
      if (game_device_data.pause_snapshot.valid)
      {
         const auto& snap = game_device_data.pause_snapshot;
         ImGui::NewLine();
         ImGui::TextUnformatted("Snapshot at last ESC press:");
         if (ImGui::BeginTable("gbfr_pause_snapshot", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
         {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Render Resolution");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%dx%d", (int)snap.render_resolution.x, (int)snap.render_resolution.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Output Resolution");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%dx%d", (int)snap.output_resolution.x, (int)snap.output_resolution.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Render Scale");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.0f%%", snap.render_scale_pct);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.jitter.x, snap.jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter from Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.table_jitter.x, snap.table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.prev_jitter.x, snap.prev_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter from Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.prev_table_jitter.x, snap.prev_table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("TAA Enabled");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(snap.taa_enabled ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Upscaling (TUP)");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(!snap.settings_obj_valid ? "N/A" : (snap.upscaling_disabled ? "Disabled" : "Enabled"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("DRS Active");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(!snap.settings_obj_valid ? "N/A" : (snap.drs_active ? "Yes" : "No"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("TAA Output Texture");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(snap.taa_output_ready ? "Ready" : "Null");

            ImGui::EndTable();
         }
      }
#endif
   }
#endif // DEVELOPMENT || TEST

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Granblue Fantasy Relink\" is developed by Izueh and is open source and free.\nIf you enjoy it, consider donating");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_izueh = std::string("Buy Izueh a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_izueh.c_str()))
      {
         system("start https://ko-fi.com/izueh");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
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
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nIzueh"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nSafetyHook"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Granblue Fantasy Relink");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      // Outline prefilter and CS hashes (depth source for NewAA mode)
      shader_hashes_OutlinePrefilter.pixel_shaders.emplace(std::stoul("897DB2C0", nullptr, 16));
      shader_hashes_OutlineCS.compute_shaders.emplace(std::stoul("DA85F5BB", nullptr, 16));

      // PostAA temporal upsampling pass hash
      shader_hashes_Temporal_Upscale.pixel_shaders.emplace(std::stoul("6EEF1071", nullptr, 16));

      // TAA / NewAA pixel shader hashes
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("478E345C", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("E49E117A", nullptr, 16)); // RenoDX compatibility
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("14393629", nullptr, 16)); // TAA (engine-native at <100% scale)
      shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("60F0256B", nullptr, 16));
      shader_hashes_MotionBlur.pixel_shaders.emplace(std::stoul("45841F6D", nullptr, 16));
      shader_hashes_MotionBlurDenoise.pixel_shaders.emplace(std::stoul("199A3FBC", nullptr, 16));
      shader_hashes_CutsceneGamma.pixel_shaders.emplace(std::stoul("1085E11F", nullptr, 16));
      shader_hashes_CutsceneColorGrade.pixel_shaders.emplace(std::stoul("50BE35B0", nullptr, 16));
      shader_hashes_CutsceneOverlayBlend.pixel_shaders.emplace(std::stoul("4517077B", nullptr, 16));
      shader_hashes_CutsceneOverlayBlend.vertex_shaders.emplace(std::stoul("DAFDA220", nullptr, 16));
      shader_hashes_CutsceneOverlayModulate.pixel_shaders.emplace(std::stoul("B9AFD904", nullptr, 16));
      shader_hashes_CutsceneOverlayModulate.vertex_shaders.emplace(std::stoul("4741FB87", nullptr, 16));

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_typeless,
         reshade::api::format::r11g11b10_float,
         reshade::api::format::r10g10b10a2_unorm};

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

#if DEVELOPMENT
   forced_shader_names.emplace(std::stoul("897DB2C0", nullptr, 16), "Outline Prefilter");
      forced_shader_names.emplace(std::stoul("DA85F5BB", nullptr, 16), "OutlineCS (depth)");
      forced_shader_names.emplace(std::stoul("6EEF1071", nullptr, 16), "Temporal Upscale");
      forced_shader_names.emplace(std::stoul("14393629", nullptr, 16), "TAA (for <100% scale)");
      forced_shader_names.emplace(std::stoul("478E345C", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("E49E117A", nullptr, 16), "TAA RenoDX");
      forced_shader_names.emplace(std::stoul("45841F6D", nullptr, 16), "Motion Blur");
      forced_shader_names.emplace(std::stoul("1085E11F", nullptr, 16), "Cutscene Gamma");
      forced_shader_names.emplace(std::stoul("50BE35B0", nullptr, 16), "Cutscene Color Grade");
      forced_shader_names.emplace(std::stoul("B9AFD904", nullptr, 16), "Cutscene Overlay Modulate");
      forced_shader_names.emplace(std::stoul("4517077B", nullptr, 16), "Cutscene Overlay Blend");
#endif
      enable_samplers_upgrade = true;

      // Set default buffer values
      // cb_luma_global_settings.GameSettings.TonemapAfterSR = true;
      cb_luma_global_settings.GameSettings.Exposure = 1.f;
      cb_luma_global_settings.GameSettings.Highlights = 1.f;
      cb_luma_global_settings.GameSettings.HighlightContrast = 1.f;
      cb_luma_global_settings.GameSettings.Shadows = 1.f;
      cb_luma_global_settings.GameSettings.ShadowContrast = 1.f;
      cb_luma_global_settings.GameSettings.Contrast = 1.f;
      cb_luma_global_settings.GameSettings.Flare = 0.f;
      cb_luma_global_settings.GameSettings.Gamma = 1.f;
      cb_luma_global_settings.GameSettings.Saturation = 1.f;
      cb_luma_global_settings.GameSettings.Dechroma = 0.f;
      cb_luma_global_settings.GameSettings.HighlightSaturation = 1.f;
      cb_luma_global_settings.GameSettings.HueEmulation = 0.f;
      cb_luma_global_settings.GameSettings.PurityEmulation = 0.f;
      cb_luma_global_settings.GameSettings.BloomType = 1; // Default to HDR bloom
      cb_luma_global_settings.GameSettings.BloomStrength = 1.f;

      game = new GranblueFantasyRelink();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_rt_creation_hook.reset();
      g_update_screen_resolution_hook.reset();
      g_VSSetConstantBuffers1_hook_immediate.reset();
      g_VSSetConstantBuffers1_hook_deferred.reset();
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
