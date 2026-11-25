#include "Effects/SR/D3D12/D3D12XeSS.hpp"

#include <climits>
#include <cmath>
#include <filesystem>
#include <format>

#include <dxgi1_4.h>

#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"

#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
#include <xess/xess_d3d12.h>
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
namespace XeSS {
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
namespace {
constexpr UINT VENDOR_ID_INTEL = 0x8086;
constexpr const wchar_t* XESS_DLL = L"libxess.dll";

bool Failed(xess_result_t r)
{
   return r < XESS_RESULT_SUCCESS;
}

bool IsIntelAdapter(ID3D12Device* device)
{
   ComPtr<IDXGIFactory4> factory;
   ComPtr<IDXGIAdapter1> adapter;
   DXGI_ADAPTER_DESC1 desc = {};
   return SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) &&
      SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == VENDOR_ID_INTEL;
}
} // namespace
#endif

bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D12Device* device)
{
   Deinit(state);
   state.capabilities.initialized = true;
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
   if (!device)
      return false;
   const std::filesystem::path folder = System::Modules::GetPath(System::Modules::GetSelf()).parent_path();
   HMODULE mod = LoadLibraryExW((folder / XESS_DLL).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
   if (!mod)
   {
      Log::Info("SR: libxess.dll not found next to Luma, D3D12 XeSS unavailable");
      return false;
   }
   Hooks::Scope::AddIgnoredModule(mod);
   state.module = mod;
   state.device = device;
   state.fn_create_context = reinterpret_cast<void*>(GetProcAddress(mod, "xessD3D12CreateContext"));
   const auto get_version = reinterpret_cast<decltype(&xessGetVersion)>(GetProcAddress(mod, "xessGetVersion"));
   state.fn_init_12 = reinterpret_cast<void*>(GetProcAddress(mod, "xessD3D12Init"));
   state.fn_execute_12 = reinterpret_cast<void*>(GetProcAddress(mod, "xessD3D12Execute"));
   state.fn_destroy_context = reinterpret_cast<void*>(GetProcAddress(mod, "xessDestroyContext"));
   state.fn_get_optimal = reinterpret_cast<void*>(GetProcAddress(mod, "xessGetOptimalInputResolution"));
   state.fn_set_velocity_scale = reinterpret_cast<void*>(GetProcAddress(mod, "xessSetVelocityScale"));
   if (!state.fn_create_context || !get_version || !state.fn_init_12 || !state.fn_execute_12 || !state.fn_destroy_context || !state.fn_get_optimal ||
      !state.fn_set_velocity_scale)
   {
      Deinit(state);
      return false;
   }
   Hooks::Scope::Guard scope;
   auto create_ctx = reinterpret_cast<decltype(&xessD3D12CreateContext)>(state.fn_create_context);
   if (Failed(create_ctx(device, reinterpret_cast<xess_context_handle_t*>(&state.context))))
   {
      Deinit(state);
      return false;
   }

   struct RetiredXeSS
   {
      void* context = nullptr;
      void* fn_destroy = nullptr;
   };

   auto* retired = new RetiredXeSS{state.context, state.fn_destroy_context};
   ComPtr<IUnknown> holder;
   holder.Attach(Hooks::Lifetime::CreateStateHolder(retired, [](void* p) {
      auto* r = static_cast<RetiredXeSS*>(p);
      if (r->context && r->fn_destroy)
      {
         Hooks::Scope::Guard guard;
         reinterpret_cast<decltype(&xessDestroyContext)>(r->fn_destroy)(static_cast<xess_context_handle_t>(r->context));
      }
      delete r;
   }));
   Hooks::Lifetime::D3D12::Replace(state.context_lifetime, std::move(holder));
   xess_version_t ver = {};
   get_version(&ver);
   state.capabilities.supported = true;
   state.capabilities.auto_selectable = IsIntelAdapter(device);
   state.capabilities.min_resolution = 32;
   state.capabilities.provider_version = std::format("{}.{}.{} (DX12)", ver.major, ver.minor, ver.patch);
   Log::Info("SR: D3D12 XeSS {} initialized", state.capabilities.provider_version);
   return true;
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state) {}

void Deinit([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
   state.context = nullptr;
   Hooks::Lifetime::D3D12::Replace(state.context_lifetime, nullptr);
   if (state.module)
   {
      Hooks::Scope::RemoveIgnoredModule(state.module);
      FreeLibrary(state.module);
      state.module = nullptr;
   }
#endif
   state.device.Reset();
   state.context_initialized = false;
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
   if (!list || !state.capabilities.supported || !state.context)
      return false;
   if (state.context_initialized && state.settings == settings)
      return true;
   Hooks::Scope::Guard scope;
   if (state.context_initialized && state.fn_create_context && state.device)
   {
      // Re-initializing XeSS in place while in-flight on the GPU is unsafe; create a fresh context and retire the old one by fence
      void* next_ctx = nullptr;
      auto create_ctx = reinterpret_cast<decltype(&xessD3D12CreateContext)>(state.fn_create_context);
      if (Failed(create_ctx(state.device.Get(), reinterpret_cast<xess_context_handle_t*>(&next_ctx))))
         return false;

      struct RetiredXeSS
      {
         void* context = nullptr;
         void* fn_destroy = nullptr;
      };

      auto* retired = new RetiredXeSS{next_ctx, state.fn_destroy_context};
      ComPtr<IUnknown> holder;
      holder.Attach(Hooks::Lifetime::CreateStateHolder(retired, [](void* p) {
         auto* r = static_cast<RetiredXeSS*>(p);
         if (r->context && r->fn_destroy)
         {
            Hooks::Scope::Guard guard;
            reinterpret_cast<decltype(&xessDestroyContext)>(r->fn_destroy)(static_cast<xess_context_handle_t>(r->context));
         }
         delete r;
      }));
      Hooks::Lifetime::D3D12::Replace(state.context_lifetime, std::move(holder));
      state.context = next_ctx;
   }
   state.context_initialized = false;

   const xess_2d_t output_res = {.x = settings.output_width, .y = settings.output_height};
   auto get_optimal = reinterpret_cast<decltype(&xessGetOptimalInputResolution)>(state.fn_get_optimal);
   xess_quality_settings_t quality = XESS_QUALITY_SETTING_AA;
   if (settings.dynamic_resolution)
      quality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
   else if (settings.render_width < settings.output_width || settings.render_height < settings.output_height)
   {
      uint32_t best_delta = UINT_MAX;
      for (int i = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE; i < XESS_QUALITY_SETTING_AA; i++)
      {
         xess_2d_t opt = {}, mn = {}, mx = {};
         if (Failed(get_optimal(static_cast<xess_context_handle_t>(state.context), &output_res, xess_quality_settings_t(i), &opt, &mn, &mx)))
            continue;
         const uint32_t delta = uint32_t(std::abs(int(opt.x) - int(settings.render_width)) + std::abs(int(opt.y) - int(settings.render_height)));
         if (delta < best_delta)
         {
            best_delta = delta;
            quality = xess_quality_settings_t(i);
         }
      }
   }

   uint32_t init_flags = 0;
   if (settings.inverted_depth)
      init_flags |= XESS_INIT_FLAG_INVERTED_DEPTH;
   if (settings.mvs_jittered)
      init_flags |= XESS_INIT_FLAG_JITTERED_MV;
   if (settings.auto_exposure)
      init_flags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
   if (!settings.hdr)
      init_flags |= XESS_INIT_FLAG_LDR_INPUT_COLOR;

   const xess_d3d12_init_params_t init_params = {.outputResolution = output_res, .qualitySetting = quality, .initFlags = init_flags};
   const xess_result_t res =
      reinterpret_cast<decltype(&xessD3D12Init)>(state.fn_init_12)(static_cast<xess_context_handle_t>(state.context), &init_params);
   if (Failed(res))
   {
      if (state.logged_errors++ < 10)
         Log::Error("SR: D3D12 xessD3D12Init failed ({})", int(res));
      return false;
   }
   reinterpret_cast<decltype(&xessSetVelocityScale)>(state.fn_set_velocity_scale)(
      static_cast<xess_context_handle_t>(state.context), settings.mvs_x_scale, settings.mvs_y_scale);
   state.context_initialized = true;
   state.settings = settings;
   return true;
#else
   return false;
#endif
}

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const DrawInputs<ID3D12Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS
   if (!state.capabilities.supported || !state.context_initialized || !list || !inputs.source_color || !inputs.output_color || !inputs.depth_buffer ||
      !inputs.motion_vectors)
      return false;
   Hooks::Lifetime::D3D12::Reference(list, state.context_lifetime);
   Hooks::Scope::Guard scope;
   xess_d3d12_execute_params_t exec = {
      .pColorTexture = inputs.source_color,
      .pVelocityTexture = inputs.motion_vectors,
      .pDepthTexture = inputs.depth_buffer,
      .pOutputTexture = inputs.output_color,
      .jitterOffsetX = params.jitter_x,
      .jitterOffsetY = params.jitter_y,
      .exposureScale = params.pre_exposure == 0.f ? 1.f : (1.f / params.pre_exposure),
      .resetHistory = uint32_t(params.reset),
      .inputWidth = params.render_width ? params.render_width : state.settings.render_width,
      .inputHeight = params.render_height ? params.render_height : state.settings.render_height,
   };
   const xess_result_t res =
      reinterpret_cast<decltype(&xessD3D12Execute)>(state.fn_execute_12)(static_cast<xess_context_handle_t>(state.context), list, &exec);
   if (Failed(res) && state.logged_errors++ < 10)
      Log::Error("SR: D3D12 xessD3D12Execute failed ({})", int(res));
   return !Failed(res);
#else
   return false;
#endif
}

int GetJitterPhases(const State& state)
{
   if (state.settings.render_width == 0)
      return GetDefaultJitterPhases();
   const float scale = float(state.settings.output_width) / float(state.settings.render_width);
   return int(std::ceil(float(GetDefaultJitterPhases()) * scale * scale));
}
} // namespace XeSS
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
