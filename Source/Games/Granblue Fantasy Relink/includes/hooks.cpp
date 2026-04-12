#include "..\..\Core\core.hpp"
#include "cbuffers.h"
#include "hooks.hpp"
#include "common.hpp"

bool TryReadCameraJitter(float2& out_jitter)
{
   const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
   if (mod_base == 0)
      return false;

   const uintptr_t camera = mod_base + kCameraGlobal_RVA;
   const uintptr_t projection_ptr = *reinterpret_cast<const uintptr_t*>(camera + kCameraProjectionDataOffset);
   if (projection_ptr == 0)
      return false;

   out_jitter.x = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterXOffset);
   out_jitter.y = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterYOffset);
   return true;
}

void OnJitterWrite(safetyhook::Context& ctx)
{
   g_hook_globals.table_jitter_x_bits.store(static_cast<uint32_t>(ctx.rcx), std::memory_order_release);
   g_hook_globals.table_jitter_y_bits.store(static_cast<uint32_t>(ctx.rax), std::memory_order_release);
   g_hook_globals.table_jitter_valid.store(true, std::memory_order_release);
}

bool TryReadTableJitter(float2& out_jitter)
{
   if (!g_hook_globals.table_jitter_valid.load(std::memory_order_acquire))
      return false;
   const uint32_t x_bits = g_hook_globals.table_jitter_x_bits.load(std::memory_order_relaxed);
   const uint32_t y_bits = g_hook_globals.table_jitter_y_bits.load(std::memory_order_relaxed);
   std::memcpy(&out_jitter.x, &x_bits, sizeof(float));
   std::memcpy(&out_jitter.y, &y_bits, sizeof(float));
   return true;
}

void PatchJitterPhases()
{
   static_assert((JITTER_PHASES & (JITTER_PHASES - 1)) == 0, "JITTER_PHASES must be a power of 2");
   static_assert(JITTER_PHASES >= 1 && JITTER_PHASES <= 64, "JITTER_PHASES must be between 1 and 64");

   const uintptr_t base_addr = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
   if (base_addr == 0)
      return;

   constexpr uint8_t mask = static_cast<uint8_t>(JITTER_PHASES - 1);
   const uintptr_t patch_addrs[2] = {
      base_addr + kJitterPhaseMask_CL_RVA,
      base_addr + kJitterPhaseMask_EAX_RVA,
   };
   for (uintptr_t addr : patch_addrs)
   {
      auto* byte_ptr = reinterpret_cast<uint8_t*>(addr);
      DWORD old_protect;
      VirtualProtect(byte_ptr, 1, PAGE_EXECUTE_READWRITE, &old_protect);
      *byte_ptr = mask;
      VirtualProtect(byte_ptr, 1, old_protect, &old_protect);
   }
}

bool IsTAARunningThisFrame()
{
   const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
   if (mod_base == 0)
      return false;
   const uintptr_t settings_obj = *reinterpret_cast<const uintptr_t*>(mod_base + kTAASettingsGlobal_RVA);
   if (settings_obj == 0)
      return false;
   return (*reinterpret_cast<const uint8_t*>(settings_obj + 22) & 1) != 0;
}

float GetEffectiveRenderScale(bool taa_running)
{
   if (!taa_running && render_scale < 1.0f)
   {
      return 1.0f;
   }
   return render_scale;
}

void* GetVTableFunction(void* obj, size_t index)
{
   void** vtable = *reinterpret_cast<void***>(obj);
   return vtable[index];
}

char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height)
{
   bool taa_running = IsTAARunningThisFrame();
   int init_width = screen_width;
   int init_height = screen_height;

   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   if (device_data && device_data->game && screen_width > 0 && screen_height > 0)
   {
      auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

      float scale = GetEffectiveRenderScale(taa_running);
      cb_luma_global_settings.GameSettings.RenderScale = scale;
      device_data->cb_luma_global_settings_dirty = true;

      {
         const float output_w = device_data->output_resolution.x > 0.f ? device_data->output_resolution.x : static_cast<float>(screen_width);
         const float output_h = device_data->output_resolution.y > 0.f ? device_data->output_resolution.y : static_cast<float>(screen_height);
         const double aspect_ratio = static_cast<double>(output_w) / output_h;
         auto render_dims = Math::FindClosestIntegerResolutionForAspectRatio(
            output_w * static_cast<double>(scale),
            output_h * static_cast<double>(scale),
            aspect_ratio);
         device_data->render_resolution.x = static_cast<float>(render_dims[0]);
         device_data->render_resolution.y = static_cast<float>(render_dims[1]);
         init_width = static_cast<int>((std::max)(1u, render_dims[0]));
         init_height = static_cast<int>((std::max)(1u, render_dims[1]));

         const uintptr_t mod_base_rt = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
         *reinterpret_cast<uint32_t*>(mod_base_rt + kRenderWidth_RVA) = static_cast<uint32_t>(init_width);
         *reinterpret_cast<uint32_t*>(mod_base_rt + kRenderHeight_RVA) = static_cast<uint32_t>(init_height);
      }
   }

   char result = g_rt_creation_hook.unsafe_call<char>(init_width, init_height);

   if (device_data && device_data->game)
   {
      auto& game_device_data_rt = *static_cast<GameDeviceDataGBFR*>(device_data->game);
      if (device_data->render_resolution.x > 0 && device_data->render_resolution.x < device_data->output_resolution.x)
      {
         const uintptr_t mod_base_rt2 = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
         *reinterpret_cast<uint32_t*>(mod_base_rt2 + kRenderWidth_RVA) = static_cast<uint32_t>(device_data->render_resolution.x);
         *reinterpret_cast<uint32_t*>(mod_base_rt2 + kRenderHeight_RVA) = static_cast<uint32_t>(device_data->render_resolution.y);
      }
   }

   return result;
}

__int64 __fastcall Hooked_UpdateScreenResolution(__int64 a1)
{
   __int64 result = g_update_screen_resolution_hook.unsafe_call<__int64>(a1);

   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   if (device_data && device_data->game)
   {
      auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

      const uintptr_t mod_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
      const uint32_t native_w = *reinterpret_cast<const uint32_t*>(mod_base + kRenderWidth_RVA);
      const uint32_t native_h = *reinterpret_cast<const uint32_t*>(mod_base + kRenderHeight_RVA);

      if (native_w > 0 && native_h > 0)
      {
         device_data->output_resolution.x = static_cast<float>(native_w);
         device_data->output_resolution.y = static_cast<float>(native_h);

         float scale = GetEffectiveRenderScale(IsTAARunningThisFrame());
         {
            const double aspect_ratio = static_cast<double>(device_data->output_resolution.x) / device_data->output_resolution.y;
            auto render_dims = Math::FindClosestIntegerResolutionForAspectRatio(
               device_data->output_resolution.x * static_cast<double>(scale),
               device_data->output_resolution.y * static_cast<double>(scale),
               aspect_ratio);
            device_data->render_resolution.x = static_cast<float>((std::max)(1u, render_dims[0]));
            device_data->render_resolution.y = static_cast<float>((std::max)(1u, render_dims[1]));
         }

         *reinterpret_cast<uint32_t*>(mod_base + kRenderWidth_RVA) = static_cast<uint32_t>(device_data->render_resolution.x);
         *reinterpret_cast<uint32_t*>(mod_base + kRenderHeight_RVA) = static_cast<uint32_t>(device_data->render_resolution.y);
      }
   }

   return result;
}

void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants)
{
   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   ID3D11Device* native_device = g_native_device_ptr.load(std::memory_order_acquire);
   if (!device_data || !native_device)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: device_data or native_device null");
      return;
   }

   auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

   constexpr UINT scene_buffer_size = sizeof(cbSceneBuffer);
   const UINT scene_buffer_constants = scene_buffer_size / 16;
   if (numConstants < scene_buffer_constants)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: numConstants too small");
      return;
   }

   auto it = device_data->native_compute_shaders.find(CompileTimeStringHash("GBFR Patch SceneBuffer"));
   if (it == device_data->native_compute_shaders.end() || !it->second)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: compute shader not found");
      return;
   }

   if (!game_device_data.scratch_scene_buffer || !game_device_data.scratch_scene_buffer_uav)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: scratch buffer or UAV missing");
      return;
   }

   DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
   compute_state_stack.Cache(pContext, device_data->uav_max_count);

   if (device_data->luma_instance_data)
   {
      ID3D11Buffer* luma_cbs[] = {device_data->luma_instance_data.get()};
      pContext->CSSetConstantBuffers(8, 1, luma_cbs);
   }

   {
      ID3D11Buffer* cbs[] = {pBuffer};
      UINT firsts[] = {firstConstant};
      UINT counts[] = {numConstants};
      pContext->CSSetConstantBuffers1(0, 1, cbs, firsts, counts);
   }

   ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_scene_buffer_uav.get()};
   pContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

   pContext->CSSetShader(it->second.get(), nullptr, 0);
   pContext->Dispatch(1, 1, 1);

   ID3D11UnorderedAccessView* null_uavs[] = {nullptr};
   pContext->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

   D3D11_BOX src_box = {};
   src_box.left = 0;
   src_box.right = scene_buffer_size;
   src_box.top = 0;
   src_box.bottom = 1;
   src_box.front = 0;
   src_box.back = 1;
   pContext->CopySubresourceRegion(
      pBuffer,
      0,
      firstConstant * 16,
      0,
      0,
      game_device_data.scratch_scene_buffer.get(),
      0,
      &src_box);

   compute_state_stack.Restore(pContext);
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

void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Deferred(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants)
{
#if PATCH_SCENE_BUFFER
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

         bool expected = false;
         if (game_device_data.scene_buffer_collect_guard.compare_exchange_strong(expected, true, std::memory_order_relaxed))
         {
            game_device_data.pending_scene_buffer = ppConstantBuffers[0];
            game_device_data.pending_first_constant = current_first_constant;
            game_device_data.pending_num_constants = pNumConstants[0];
            game_device_data.scene_buffer_info_collected.store(true, std::memory_order_release);
         }
      }
   }
#endif

   g_VSSetConstantBuffers1_hook_deferred.unsafe_call<void>(
      pContext, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
