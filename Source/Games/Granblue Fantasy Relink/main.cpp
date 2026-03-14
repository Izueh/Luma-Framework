// Granblue Fantasy Relink - DLAA/FSRAA Implementation
// Replaces TAA with DLSS/FSR in DLAA (no upscaling) mode
// Based on the FFXV Luma implementation

#define GAME_GRANBLUE_FANTASY_RELINK 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define JITTER_PHASES 8

#include <d3d11.h>
#include "..\..\Core\core.hpp"
#include "includes\cbuffers.h"
#include "includes\safetyhook.hpp"

namespace
{

   struct GameDeviceDataGBFR final : public GameDeviceData
   {
#if ENABLE_SR
      // SR - Resources extracted from TAA pass
      com_ptr<ID3D11Resource> sr_source_color;    // t3: current color
      com_ptr<ID3D11Resource> depth_buffer;        // t5: depth
      com_ptr<ID3D11Resource> sr_motion_vectors;   // t23: motion vectors (already decoded)
      com_ptr<ID3D11Resource> taa_rt1_resource;
      D3D11_TEXTURE2D_DESC taa_rt1_desc;
      std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;
      ID3D11CommandList* remainder_command_list = nullptr; // Raw pointer for identity comparison only (no AddRef to avoid desync with reshade's proxy ref tracking)
      com_ptr<ID3D11CommandList> partial_command_list;
      com_ptr<ID3D11Buffer> modifiable_index_vertex_buffer;
      std::atomic<bool> output_supports_uav = false;
      std::atomic<bool> output_changed = false;

#endif // ENABLE_SR

      float camera_fov = 60.0f * (3.14159265f / 180.0f);
      float camera_near = 0.1f;
      float camera_far = 1000.0f;
      float2 jitter = {0, 0};
      float2 prev_jitter = {0, 0};      // Jitter from the previous frame (for g_PrevProj)

      // Scene buffer patching resources
      com_ptr<ID3D11Buffer> scratch_scene_buffer;           // Structured buffer for compute shader output (sizeof(cbSceneBuffer))
      com_ptr<ID3D11UnorderedAccessView> scratch_scene_buffer_uav;
      std::atomic<bool> scene_buffer_patched_this_frame = false;

      // Pending scene buffer patch info: written by the deferred context hook,
      // consumed by execute_secondary_command_list on the immediate context.
      std::atomic<bool> scene_buffer_collect_guard = false;  // CAS guard so only one deferred thread writes
      std::atomic<bool> scene_buffer_info_collected = false;  // Release-stored after pending fields are written
      ID3D11Buffer* pending_scene_buffer = nullptr;
      UINT pending_first_constant = 0;
      UINT pending_num_constants = 0;
      std::mutex scene_buffer_bindings_mutex;
      std::set<UINT> scene_buffer_offsets_this_frame;
   };

   struct JitterEntry
   {
      float x;
      float y;
   };

   ShaderHashesList shader_hashes_TAA;
   const uint32_t CBSceneBuffer_size = sizeof(cbSceneBuffer);

   constexpr std::array<JitterEntry, JITTER_PHASES> precomputed_jitters = []() {
      std::array<JitterEntry, JITTER_PHASES> entries{};
      for (unsigned int i = 0; i < entries.size(); i++)
      {
         entries[i] = JitterEntry{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
      }
      return entries;
   }();

   constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;

   SafetyHookInline g_VSSetConstantBuffers1_hook_immediate;
   SafetyHookInline g_VSSetConstantBuffers1_hook_deferred;

   // Global pointers set during OnCreateDevice, used by the hook functions
   // to access Luma device data without going through reshade API.
   std::atomic<DeviceData*> g_device_data_ptr = nullptr;
   std::atomic<ID3D11Device*> g_native_device_ptr = nullptr;

   static void* GetVTableFunction(void* obj, size_t index)
   {
      void** vtable = *reinterpret_cast<void***>(obj);
      return vtable[index];
   }

   void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Immediate(
      ID3D11DeviceContext1* pContext,
      UINT StartSlot,
      UINT NumBuffers,
      ID3D11Buffer* const* ppConstantBuffers,
      const UINT* pFirstConstant,
      const UINT* pNumConstants)
   {
      g_VSSetConstantBuffers1_hook_immediate.unsafe_call<void>(
         pContext, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
   }

   // Hook function for deferred context calls.
   // Detects the first VSSetConstantBuffers1 call with StartSlot=0 each frame
   // and collects the buffer info for later patching on the immediate context.
   void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Deferred(
      ID3D11DeviceContext1* pContext,
      UINT StartSlot,
      UINT NumBuffers,
      ID3D11Buffer* const* ppConstantBuffers,
      const UINT* pFirstConstant,
      const UINT* pNumConstants)
   {
      // Detect VSSetConstantBuffers1 with StartSlot=0 and exactly 48 constants (SceneBuffer)
      // and store the buffer info so the immediate context can patch it later.
      // numConstants == 48 is the unique fingerprint of the SceneBuffer bind; other slot-0
      // binds in the frame use different constant counts.
      if (StartSlot == 0 && NumBuffers >= 1 && ppConstantBuffers && ppConstantBuffers[0] &&
          pFirstConstant && pNumConstants && pNumConstants[0] == 48)
      {
         DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
         if (device_data)
         {
            auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);
            const UINT current_first_constant = pFirstConstant[0];

            {
               std::lock_guard<std::mutex> lock(game_device_data.scene_buffer_bindings_mutex);
               game_device_data.scene_buffer_offsets_this_frame.insert(current_first_constant);
            }

            // Set pending fields on first capture per frame (CAS ensures only one writer).
            bool expected = false;
            if (game_device_data.scene_buffer_collect_guard.compare_exchange_strong(expected, true, std::memory_order_relaxed))
            {
               game_device_data.pending_scene_buffer = ppConstantBuffers[0];
               game_device_data.pending_first_constant = current_first_constant;
               game_device_data.pending_num_constants = pNumConstants[0];
               // Release ensures the three stores above are visible to any thread
               // that does an acquire-load of scene_buffer_info_collected.
               game_device_data.scene_buffer_info_collected.store(true, std::memory_order_release);
            }
         }
      }

      g_VSSetConstantBuffers1_hook_deferred.unsafe_call<void>(
         pContext, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
   }

   // Patches the captured SceneBuffer range on the immediate context before
   // command-list replay, so all later draws read Luma jitter instead of engine jitter.
   static void PatchSceneBufferInHook(
      ID3D11DeviceContext1* pContext,
      ID3D11Buffer* pBuffer,
      UINT firstConstant,
      UINT numConstants)
   {
      DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
      ID3D11Device* native_device = g_native_device_ptr.load(std::memory_order_acquire);
      if (!device_data || !native_device)
      {
         ASSERT_ONCE(false && "PatchSceneBufferInHook: device_data or native_device null");
         return;
      }

      auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

      // Verify the SceneBuffer region fits within the reported constants
      const UINT scene_buffer_constants = CBSceneBuffer_size / 16; // 544 / 16 = 34 constants
      if (numConstants < scene_buffer_constants)
      {
         ASSERT_ONCE(false && "PatchSceneBufferInHook: numConstants too small");
         return;
      }

      // Retrieve the compute shader
      auto it = device_data->native_compute_shaders.find(CompileTimeStringHash("GBFR Patch SceneBuffer"));
      if (it == device_data->native_compute_shaders.end() || !it->second)
      {
         ASSERT_ONCE(false && "PatchSceneBufferInHook: compute shader not found");
         return;
      }

      // Ensure scratch buffer and UAV exist
      if (!game_device_data.scratch_scene_buffer || !game_device_data.scratch_scene_buffer_uav)
      {
         ASSERT_ONCE(false && "PatchSceneBufferInHook: scratch buffer or UAV missing");
         return;
      }

      // Cache compute state before our dispatch
      DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
      compute_state_stack.Cache(pContext, device_data->uav_max_count);

      // Bind the Luma instance data constant buffer for compute stage (contains jitter values)
      // We need to update and bind it manually since we're in the hook, not in OnDrawOrDispatch
      if (device_data->luma_instance_data)
      {
         const float resX = device_data->output_resolution.x;
         const float resY = device_data->output_resolution.y;
         if (resX > 0.f && resY > 0.f)
         {
            CB::LumaInstanceDataPadded instance_data = {};
            instance_data.GameData.JitterOffset.x     = game_device_data.jitter.x * 2.0f / resX;
            instance_data.GameData.JitterOffset.y     = game_device_data.jitter.y * -2.0f / resY;
            instance_data.GameData.PrevJitterOffset.x = game_device_data.prev_jitter.x * 2.0f / resX;
            instance_data.GameData.PrevJitterOffset.y = game_device_data.prev_jitter.y * -2.0f / resY;
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(pContext->Map(device_data->luma_instance_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
               std::memcpy(mapped.pData, &instance_data, sizeof(instance_data));
               pContext->Unmap(device_data->luma_instance_data.get(), 0);
            }
         }
         ID3D11Buffer* luma_cbs[] = {device_data->luma_instance_data.get()};
         // luma_data_cbuffer_index = 8, hardcoded since we're outside the class
         pContext->CSSetConstantBuffers(8, 1, luma_cbs);
      }

      // Set the original SceneBuffer as CB slot 0 input for the compute shader.
      // This uses the same buffer/firstConstant/numConstants that was being set
      // for the VS, so the compute shader reads the correct ring buffer region.
      {
         ID3D11Buffer* cbs[] = {pBuffer};
         UINT firsts[] = {firstConstant};
         UINT counts[] = {numConstants};
         pContext->CSSetConstantBuffers1(0, 1, cbs, firsts, counts);
      }

      // Bind scratch buffer UAV
      ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_scene_buffer_uav.get()};
      pContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

      // Set and dispatch the patch compute shader
      pContext->CSSetShader(it->second.get(), nullptr, 0);
      pContext->Dispatch(1, 1, 1);

      // Unbind UAV to avoid resource hazards
      ID3D11UnorderedAccessView* null_uavs[] = {nullptr};
      pContext->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

      // Copy the patched SceneBuffer from the scratch buffer back into the global ring buffer
      // at the correct offset (firstConstant * 16 bytes)
      D3D11_BOX src_box = {};
      src_box.left = 0;
      src_box.right = CBSceneBuffer_size;
      src_box.top = 0;
      src_box.bottom = 1;
      src_box.front = 0;
      src_box.back = 1;
      pContext->CopySubresourceRegion(
         pBuffer,           // Destination: the global ring buffer
         0,                 // DstSubresource
         firstConstant * 16, // DstX: byte offset into the ring buffer
         0, 0,              // DstY, DstZ
         game_device_data.scratch_scene_buffer.get(), // Source: patched data
         0,                 // SrcSubresource
         &src_box);         // Region to copy

      // Restore compute state
      compute_state_stack.Restore(pContext);
   }

} // namespace

class GranblueFantasyRelink final : public Game
{
   static GameDeviceDataGBFR& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataGBFR*>(device_data.game);
   }

#include "includes\dlss_helpers.hpp"

public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 9;
      luma_data_cbuffer_index = 8;

      // Register native compute shader for SceneBuffer patching
      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Patch SceneBuffer"),
         ShaderDefinition{"Luma_GBFR_PatchSceneBuffer", reshade::api::pipeline_subobject_type::compute_shader});
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
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

      // =========================================================================
      // TAA Pass Handling - Replace TAA with SR (DLAA/FSRAA mode, no upscaling)
      // =========================================================================
      if (device_data.sr_type != SR::Type::None &&
          !device_data.sr_suppressed &&
          original_shader_hashes.Contains(shader_hashes_TAA))
      {

         device_data.taa_detected = true;

         // Skip until we captured the SceneBuffer binding for this frame.
         // Patching happens later on the immediate context, but still before GPU replay.
         if (!game_device_data.scene_buffer_info_collected.load(std::memory_order_acquire))
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }

         // Extract TAA shader resources (source color, depth, motion vectors)
         if (!ExtractTAAShaderResources(native_device_context, game_device_data))
         {
            ASSERT_ONCE(false);
            return DrawOrDispatchOverrideType::None;
         }

         device_data.has_drawn_sr = true;

         // Get render targets (TAA writes to RT0 and RT1)
         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         if (render_target_views[1].get() == nullptr)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Setup SR output texture.
         render_target_views[1]->GetResource(&game_device_data.taa_rt1_resource);

         if (!SetupSROutput(native_device, device_data, game_device_data))
         {
            return DrawOrDispatchOverrideType::None;
         }


         native_device_context->FinishCommandList(TRUE, &game_device_data.partial_command_list);
         if (game_device_data.modifiable_index_vertex_buffer)
         {
            D3D11_MAPPED_SUBRESOURCE mapped_buffer;
            // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
            native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
            native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
         }
         game_device_data.draw_device_context = native_device_context;
         if (device_data.has_drawn_sr)
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (native_device_context)
      {
         // This is an ExecuteCommandList callback — a command list is about to be replayed
         // on the immediate context. Patch the ring buffer before the first one runs
         // (Map/Unmap already happened on the immediate context at start of frame).
         com_ptr<ID3D11CommandList> native_command_list;
         {
            ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
            secondary_child->QueryInterface(&native_command_list);
         }
         if (native_command_list)
         {
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
                  com_ptr<ID3D11DeviceContext1> immediate_context1;
                  if (SUCCEEDED(native_device_context->QueryInterface(IID_PPV_ARGS(&immediate_context1))))
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
         }
         if (native_command_list.get() == game_device_data.remainder_command_list && game_device_data.partial_command_list.get() != nullptr)
         {
            native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
            game_device_data.partial_command_list.reset();

            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
                     // Get SR instance data
            auto* sr_instance_data = device_data.GetSRInstanceData();
            // DLAA mode: render resolution == output resolution (no upscaling)
            {
               SR::SettingsData settings_data;
               settings_data.output_width = game_device_data.taa_rt1_desc.Width;
               settings_data.output_height = game_device_data.taa_rt1_desc.Height;
               settings_data.render_width = game_device_data.taa_rt1_desc.Width;
               settings_data.render_height = game_device_data.taa_rt1_desc.Height;
               settings_data.dynamic_resolution = false;
               settings_data.hdr = true;
               settings_data.auto_exposure = true; // No exposure texture extraction for now
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false; // Granblue MVs are unjittered (g_ProjectionOffset cancels jitter in the PS)
               // MVs in g_GeometryBuffer03 are in UV space (0-1 range) with forward direction
               // (current_uv - previous_uv), so we need negative resolution scale to convert
               // to pixel space with backward direction (DLSS expects positive toward top-left)
               settings_data.mvs_x_scale = -(float)game_device_data.taa_rt1_desc.Width;
               settings_data.mvs_y_scale = -(float)game_device_data.taa_rt1_desc.Height;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
            }
            // Prepare SR draw data
            {
               bool reset_sr = device_data.force_reset_sr || game_device_data.output_changed;
               device_data.force_reset_sr = false;

               float jitter_x = game_device_data.jitter.x;
               float jitter_y = game_device_data.jitter.y;
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.sr_source_color.get();
               draw_data.output_color = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_buffer.get();
               draw_data.pre_exposure = 0.0f;
               // Pass the frame jitter tracked by this module.
               draw_data.jitter_x = jitter_x;
               draw_data.jitter_y = jitter_y;
               draw_data.vert_fov = game_device_data.camera_fov;
               draw_data.far_plane = game_device_data.camera_far;
               draw_data.near_plane = game_device_data.camera_near;
               draw_data.reset = reset_sr;
               draw_data.render_width = game_device_data.taa_rt1_desc.Width;
               draw_data.render_height = game_device_data.taa_rt1_desc.Height;

               // Cache and restore state around SR execution
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               draw_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);

               // Execute SR
               device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data);
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
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = device_data.sr_type == SR::Type::DLSS ? "DLAA" : "FSRAA";
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
               }
#endif
               draw_state_stack.Restore(native_device_context.get());
               compute_state_stack.Restore(native_device_context.get());

            }

            // Clear temporary resources
            game_device_data.sr_source_color = nullptr;
            game_device_data.depth_buffer = nullptr;
            game_device_data.sr_motion_vectors = nullptr;

            // Handle SR result
            if (device_data.has_drawn_sr)
            {
               if (!game_device_data.output_supports_uav)
               {
                  native_device_context->CopyResource(game_device_data.taa_rt1_resource.get(), device_data.sr_output_color.get());
               }
            }
            else
            {
               device_data.force_reset_sr = true;
            }
            game_device_data.taa_rt1_resource = nullptr;
            if (!game_device_data.output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }  
      }

      // FinishCommandList case: cmd_list is the new ID3D11CommandList,
      // secondary_cmd_list is the deferred context that produced it.
      com_ptr<ID3D11CommandList> finish_command_list;
      hr = device_child->QueryInterface(&finish_command_list);
      if (finish_command_list)
      {
         com_ptr<ID3D11DeviceContext> deferred_ctx;
         ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = secondary_child->QueryInterface(&deferred_ctx);
         if (deferred_ctx)
         {
            if (deferred_ctx.get() == game_device_data.draw_device_context)
            {
               game_device_data.remainder_command_list = finish_command_list.get();
            }
         }
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Create scratch structured buffer for SceneBuffer patching (sizeof(cbSceneBuffer) = 544 bytes)
      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = CBSceneBuffer_size;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = CBSceneBuffer_size;
         HRESULT hr = native_device->CreateBuffer(&bd, nullptr, &game_device_data.scratch_scene_buffer);
         ASSERT_ONCE(SUCCEEDED(hr));
      }

      // Create UAV for the scratch buffer
      if (game_device_data.scratch_scene_buffer)
      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
         uavd.Format = DXGI_FORMAT_UNKNOWN; // Required for structured buffers
         uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         uavd.Buffer.FirstElement = 0;
         uavd.Buffer.Flags = 0;
         uavd.Buffer.NumElements = 1;
         HRESULT hr = native_device->CreateUnorderedAccessView(
            game_device_data.scratch_scene_buffer.get(), &uavd, &game_device_data.scratch_scene_buffer_uav);
         ASSERT_ONCE(SUCCEEDED(hr));
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataGBFR;

      // Seed jitter so current/previous are valid on the first dispatch.
      {
         auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data.game);
         // Keep Halton samples in module space; convert to NDC offset when uploading instance data.
         game_device_data.jitter.x    = precomputed_jitters[0].x;
         game_device_data.jitter.y    = precomputed_jitters[0].y;
         game_device_data.prev_jitter = game_device_data.jitter; // no real previous yet
      }

      // Store global pointers for access from vtable hooks
      g_device_data_ptr.store(&device_data, std::memory_order_release);
      g_native_device_ptr.store(native_device, std::memory_order_release);

      // Install inline hook on VSSetConstantBuffers1 (immediate context)
      com_ptr<ID3D11DeviceContext> immediate_context;
      native_device->GetImmediateContext(&immediate_context);

      com_ptr<ID3D11DeviceContext1> immediate_context1;
      if (SUCCEEDED(immediate_context->QueryInterface(IID_PPV_ARGS(&immediate_context1))))
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
         com_ptr<ID3D11DeviceContext> deferred_context;
         if (SUCCEEDED(native_device->CreateDeferredContext(0, &deferred_context)))
         {
            com_ptr<ID3D11DeviceContext1> deferred_context1;
            if (SUCCEEDED(deferred_context->QueryInterface(IID_PPV_ARGS(&deferred_context1))))
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
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }
      device_data.has_drawn_sr = false;
      game_device_data.remainder_command_list = nullptr;
      game_device_data.draw_device_context = nullptr;
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

      game_device_data.prev_jitter = game_device_data.jitter;
      // OnPresent runs before FrameIndex increments, so +1 advances to the next frame sample.
      const JitterEntry& j = precomputed_jitters[(cb_luma_global_settings.FrameIndex + 1) % precomputed_jitters.size()];
      game_device_data.jitter.x = j.x;
      game_device_data.jitter.y = j.y;


   }
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

      // TAA pixel shader hash
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("478E345C", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("E49E117A", nullptr, 16)); // RenoDX compatibility

#if DEVELOPMENT
      force_disable_display_composition = false;
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
#else
      force_disable_display_composition = true;
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
#endif

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

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("478E345C", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("E49E117A", nullptr, 16), "TAA RenoDX");
#endif
      enable_samplers_upgrade = true;
      game = new GranblueFantasyRelink();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_VSSetConstantBuffers1_hook_immediate.reset();
      g_VSSetConstantBuffers1_hook_deferred.reset();
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
