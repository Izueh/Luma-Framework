#include "Effects/SR/D3D11/D3D11XeSS.hpp"

#include <climits>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

#include <dxgi.h>
#include <wrl/client.h>

#include "Effects/SR/D3D11/D3D12Interop.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"

#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
#include <xess/xess_d3d11.h>
#include <xess/xess_d3d12.h>
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace XeSS {
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
namespace {
constexpr UINT VENDOR_ID_INTEL = 0x8086;
constexpr const wchar_t* XESS_DLL = L"libxess.dll";
constexpr const wchar_t* XESS_DX11_DLL = L"libxess_dx11.dll";

bool Failed(xess_result_t r)
{
   return r < XESS_RESULT_SUCCESS;
}

void LogError(State& state, const std::string& message)
{
   if (state.logged_errors++ < 10)
      Log::Error("SR XeSS: {}", message);
}

bool InitInstance(State& state, ID3D11Device* device, D3D12Interop::Context* interop)
{
   state.fn_destroy_context = reinterpret_cast<void*>(GetProcAddress(state.module, "xessDestroyContext"));
   state.fn_get_optimal = reinterpret_cast<void*>(GetProcAddress(state.module, "xessGetOptimalInputResolution"));
   state.fn_set_velocity_scale = reinterpret_cast<void*>(GetProcAddress(state.module, "xessSetVelocityScale"));
   const auto get_version = reinterpret_cast<decltype(&xessGetVersion)>(GetProcAddress(state.module, "xessGetVersion"));
   if (!state.fn_destroy_context || !state.fn_get_optimal || !state.fn_set_velocity_scale || !get_version)
      return false;

   xess_version_t ver = {};
   get_version(&ver);
   xess_result_t res = XESS_RESULT_ERROR_UNKNOWN;
   Hooks::Scope::Guard scope;
   if (state.native_dx11)
   {
      const auto create_ctx = reinterpret_cast<decltype(&xessD3D11CreateContext)>(GetProcAddress(state.module, "xessD3D11CreateContext"));
      state.fn_init_11 = reinterpret_cast<void*>(GetProcAddress(state.module, "xessD3D11Init"));
      state.fn_execute_11 = reinterpret_cast<void*>(GetProcAddress(state.module, "xessD3D11Execute"));
      if (!create_ctx || !state.fn_init_11 || !state.fn_execute_11)
         return false;
      res = create_ctx(device, reinterpret_cast<xess_context_handle_t*>(&state.context));
   }
   else
   {
      const auto create_ctx = reinterpret_cast<decltype(&xessD3D12CreateContext)>(GetProcAddress(state.module, "xessD3D12CreateContext"));
      state.fn_init_12 = reinterpret_cast<void*>(GetProcAddress(state.module, "xessD3D12Init"));
      state.fn_execute_12 = reinterpret_cast<void*>(GetProcAddress(state.module, "xessD3D12Execute"));
      if (!create_ctx || !state.fn_init_12 || !state.fn_execute_12 || !interop || !D3D12Interop::Init(*interop, device))
         return false;
      state.interop = interop;
      res = create_ctx(interop->device_12.Get(), reinterpret_cast<xess_context_handle_t*>(&state.context));
   }
   if (Failed(res))
   {
      state.context = nullptr;
      return false;
   }
   state.capabilities.provider_version = std::format("{}.{}.{} ({})", ver.major, ver.minor, ver.patch, state.native_dx11 ? "DX11" : "DX12 interop");
   Log::Info("SR: D3D11 XeSS {} initialized", state.capabilities.provider_version);
   return true;
}
} // namespace
#endif

bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D11Device* device, [[maybe_unused]] D3D12Interop::Context* interop)
{
   Deinit(state);
   state.capabilities.initialized = true;
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (!device)
      return false;
   ComPtr<IDXGIDevice> dxgi_device;
   ComPtr<IDXGIAdapter> adapter;
   DXGI_ADAPTER_DESC adapter_desc = {};
   const bool is_intel = SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) && SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
      SUCCEEDED(adapter->GetDesc(&adapter_desc)) && adapter_desc.VendorId == VENDOR_ID_INTEL;

   const std::filesystem::path folder = System::Modules::GetPath(System::Modules::GetSelf()).parent_path();
   const auto try_load = [&](const wchar_t* dll_name, bool native_dx11) {
      HMODULE mod = LoadLibraryExW((folder / dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!mod)
         return false;
      Hooks::Scope::AddIgnoredModule(mod);
      state.module = mod;
      state.native_dx11 = native_dx11;
      state.capabilities.auto_selectable = is_intel;
      state.capabilities.min_resolution = 32;
      if (InitInstance(state, device, interop))
      {
         state.capabilities.supported = true;
         return true;
      }
      Deinit(state);
      state.capabilities.initialized = true;
      return false;
   };
   if (is_intel && try_load(XESS_DX11_DLL, true))
      return true;
   return try_load(XESS_DLL, false);
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state) {}

void Deinit([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (state.context && state.fn_destroy_context)
   {
      const bool safe = state.native_dx11 || !state.interop || D3D12Interop::WaitForIdle(*state.interop);
      if (safe)
      {
         Hooks::Scope::Guard scope;
         auto destroy_fn = reinterpret_cast<decltype(&xessDestroyContext)>(state.fn_destroy_context);
         destroy_fn(static_cast<xess_context_handle_t>(state.context));
      }
      state.context = nullptr;
   }
   if (state.module)
   {
      Hooks::Scope::RemoveIgnoredModule(state.module);
      FreeLibrary(state.module);
      state.module = nullptr;
   }
#endif
   state.context_initialized = false;
   state.interop = nullptr;
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (!context || !state.capabilities.supported || !state.context)
      return false;
   if (state.context_initialized && state.settings == settings)
      return true;

   Hooks::Scope::Guard scope;
   if (!state.native_dx11 && state.interop && !D3D12Interop::WaitForIdle(*state.interop))
      return false;
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

   xess_result_t res = XESS_RESULT_ERROR_UNKNOWN;
   if (state.native_dx11)
   {
      const xess_d3d11_init_params_t params = {.outputResolution = output_res, .qualitySetting = quality, .initFlags = init_flags};
      res = reinterpret_cast<decltype(&xessD3D11Init)>(state.fn_init_11)(static_cast<xess_context_handle_t>(state.context), &params);
   }
   else
   {
      const xess_d3d12_init_params_t params = {.outputResolution = output_res, .qualitySetting = quality, .initFlags = init_flags};
      res = reinterpret_cast<decltype(&xessD3D12Init)>(state.fn_init_12)(static_cast<xess_context_handle_t>(state.context), &params);
   }
   if (Failed(res))
   {
      LogError(state, std::format("XeSS init failed ({})", int(res)));
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

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const DrawInputs<ID3D11Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_XESS) && LUMA_USE_XESS && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   using Slot = D3D12Interop::TextureSlot;
   if (!state.capabilities.supported || !state.context_initialized || !inputs.source_color || !inputs.output_color || !inputs.depth_buffer ||
      !inputs.motion_vectors)
      return false;

   const auto fill_common = [&](auto& exec) {
      exec.jitterOffsetX = params.jitter_x;
      exec.jitterOffsetY = params.jitter_y;
      exec.exposureScale = params.pre_exposure == 0.f ? 1.f : (1.f / params.pre_exposure);
      exec.resetHistory = uint32_t(params.reset);
      exec.inputWidth = params.render_width ? params.render_width : state.settings.render_width;
      exec.inputHeight = params.render_height ? params.render_height : state.settings.render_height;
   };

   Hooks::Scope::Guard scope;
   if (state.native_dx11)
   {
      if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return false;
      xess_d3d11_execute_params_t exec = {
         .pColorTexture = inputs.source_color,
         .pVelocityTexture = inputs.motion_vectors,
         .pDepthTexture = inputs.depth_buffer,
         .pOutputTexture = inputs.output_color,
      };
      fill_common(exec);
      return !Failed(reinterpret_cast<decltype(&xessD3D11Execute)>(state.fn_execute_11)(static_cast<xess_context_handle_t>(state.context), &exec));
   }

   if (!state.interop)
      return false;
   const std::array<std::pair<Slot, ID3D11Resource*>, 4> slots = {{
      {Slot::Color, inputs.source_color},
      {Slot::Depth, inputs.depth_buffer},
      {Slot::MotionVectors, inputs.motion_vectors},
      {Slot::Output, inputs.output_color},
   }};
   for (const auto& [slot, res] : slots)
   {
      if (!D3D12Interop::UpdateSharedTextureFrom(*state.interop, state.interop->textures[size_t(slot)], res,
             {.is_depth = slot == Slot::Depth, .unordered_access = slot == Slot::Depth || slot == Slot::Output}))
         return false;
   }
   if (!D3D12Interop::CopyDepth(*state.interop, state.interop->textures[size_t(Slot::Depth)], context, inputs.depth_buffer))
      return false;
   context->CopyResource(state.interop->textures[size_t(Slot::Color)].resource_11.Get(), inputs.source_color);
   context->CopyResource(state.interop->textures[size_t(Slot::MotionVectors)].resource_11.Get(), inputs.motion_vectors);

   ID3D12GraphicsCommandList* const list_12 = D3D12Interop::BeginCommandList(*state.interop, context);
   if (!list_12)
      return false;

   D3D12_RESOURCE_BARRIER barriers[4] = {};
   for (size_t i = 0; i < 4; i++)
   {
      const Slot slot = slots[i].first;
      barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[i].Transition = {state.interop->textures[size_t(slot)].resource_12.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON,
         slot == Slot::Output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
   }
   list_12->ResourceBarrier(4, barriers);

   xess_d3d12_execute_params_t exec = {
      .pColorTexture = state.interop->textures[size_t(Slot::Color)].resource_12.Get(),
      .pVelocityTexture = state.interop->textures[size_t(Slot::MotionVectors)].resource_12.Get(),
      .pDepthTexture = state.interop->textures[size_t(Slot::Depth)].resource_12.Get(),
      .pOutputTexture = state.interop->textures[size_t(Slot::Output)].resource_12.Get(),
   };
   fill_common(exec);
   const bool ok =
      !Failed(reinterpret_cast<decltype(&xessD3D12Execute)>(state.fn_execute_12)(static_cast<xess_context_handle_t>(state.context), list_12, &exec));

   for (auto& b : barriers)
      std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
   list_12->ResourceBarrier(4, barriers);

   if (!D3D12Interop::ExecuteCommandList(*state.interop, context))
   {
      if (state.interop->poisoned)
         state.capabilities.supported = false;
      return false;
   }
   if (!ok)
      return false;
   context->CopyResource(inputs.output_color, state.interop->textures[size_t(Slot::Output)].resource_11.Get());
   return true;
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
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
