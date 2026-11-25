#include "Effects/SR/D3D12/D3D12Dlss.hpp"

#include <filesystem>

#include "Effects/SR/DlssCommon.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"

#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
namespace Dlss {
bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D12Device* device)
{
   Deinit(state);
   state.capabilities.initialized = true;
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   if (!device)
      return false;
   Hooks::Scope::Guard scope;
   const std::filesystem::path data_path = System::Modules::GetPath(System::Modules::GetSelf()).parent_path();
   const std::wstring data_dir = data_path.empty() ? L"." : data_path.wstring();
   NVSDK_NGX_Result result =
      NVSDK_NGX_D3D12_Init_with_ProjectID(DlssCommon::PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, DlssCommon::ENGINE_VERSION, data_dir.c_str(), device);
   if (NVSDK_NGX_FAILED(result))
   {
      Log::Info("SR: D3D12 DLSS not available (0x{:08X})", uint32_t(result));
      return false;
   }
   Hooks::Scope::AddIgnoredModule(GetModuleHandleW(L"nvngx_dlss.dll"));
   Hooks::Scope::AddIgnoredModule(GetModuleHandleW(L"_nvngx.dll"));
   state.device = device;
   state.capabilities.min_resolution = 32;
   result = NVSDK_NGX_D3D12_GetCapabilityParameters(&state.capabilities_params);
   if (NVSDK_NGX_SUCCEED(result) && state.capabilities_params)
   {
      int available = 0;
      state.capabilities_params->Get(NVSDK_NGX_EParameter_SuperSampling_Available, &available);
      state.capabilities.supported = available > 0;
      state.capabilities.auto_selectable = state.capabilities.supported;
   }
   if (state.capabilities.supported)
   {
      state.capabilities.provider_version = "NGX D3D12";
      Log::Info("SR: D3D12 DLSS initialized");
   }
   return state.capabilities.supported;
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   state.feature = nullptr;
   state.runtime_params = nullptr;
   Hooks::Lifetime::D3D12::Replace(state.feature_lifetime, nullptr);
#endif
}

void Deinit([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   ReleaseResources(state);
   Hooks::Scope::Guard scope;
   if (state.capabilities_params)
   {
      NVSDK_NGX_D3D12_DestroyParameters(state.capabilities_params);
      state.capabilities_params = nullptr;
   }
   if (state.device)
   {
      NVSDK_NGX_D3D12_Shutdown1(state.device.Get());
      state.device.Reset();
   }
#endif
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   if (!list || !state.capabilities.supported || !state.capabilities_params)
      return false;
   if (state.feature && state.runtime_params && state.settings == settings)
      return true;

   Hooks::Scope::Guard scope;
   ReleaseResources(state);

   NVSDK_NGX_Parameter* runtime_params = nullptr;
   if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_AllocateParameters(&runtime_params)) || !runtime_params)
      return false;

   const int quality_mode = DlssCommon::SelectPerfQualityMode(state.capabilities_params, settings);
   DlssCommon::ApplyRuntimeParams(runtime_params, settings);

   NVSDK_NGX_DLSS_Create_Params create_params = {};
   create_params.Feature.InTargetWidth = settings.output_width;
   create_params.Feature.InTargetHeight = settings.output_height;
   create_params.Feature.InWidth = settings.render_width;
   create_params.Feature.InHeight = settings.render_height;
   create_params.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(quality_mode);
   create_params.InFeatureCreateFlags = DlssCommon::BuildCreateFlags(settings);

   NVSDK_NGX_Handle* feature = nullptr;
   NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &feature, runtime_params, &create_params);
   if (NVSDK_NGX_FAILED(result))
   {
      NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
      create_params.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_Balanced;
      result = NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &feature, runtime_params, &create_params);
   }
   if (NVSDK_NGX_FAILED(result) || !feature)
   {
      NVSDK_NGX_D3D12_DestroyParameters(runtime_params);
      Log::Error("SR: D3D12 DLSS create feature failed (0x{:08X})", uint32_t(result));
      return false;
   }

   struct RetiredFeature
   {
      NVSDK_NGX_Handle* feature = nullptr;
      NVSDK_NGX_Parameter* params = nullptr;
   };

   auto* retired = new RetiredFeature{feature, runtime_params};
   Microsoft::WRL::ComPtr<IUnknown> holder;
   holder.Attach(Hooks::Lifetime::CreateStateHolder(retired, [](void* p) {
      auto* r = static_cast<RetiredFeature*>(p);
      Hooks::Scope::Guard guard;
      if (r->feature)
         NVSDK_NGX_D3D12_ReleaseFeature(r->feature);
      if (r->params)
         NVSDK_NGX_D3D12_DestroyParameters(r->params);
      delete r;
   }));
   Hooks::Lifetime::D3D12::Replace(state.feature_lifetime, std::move(holder));
   state.feature = feature;
   state.runtime_params = runtime_params;
   state.settings = settings;
   return true;
#else
   return false;
#endif
}

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const DrawInputs<ID3D12Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   if (!state.capabilities.supported || !state.feature || !state.runtime_params || !list)
      return false;
   Hooks::Lifetime::D3D12::Reference(list, state.feature_lifetime);
   Hooks::Scope::Guard scope;
   NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
   eval.pInDepth = inputs.depth_buffer;
   eval.pInMotionVectors = inputs.motion_vectors;
   eval.InRenderSubrectDimensions.Width = params.render_width ? params.render_width : state.settings.render_width;
   eval.InRenderSubrectDimensions.Height = params.render_height ? params.render_height : state.settings.render_height;
   eval.Feature.pInColor = inputs.source_color;
   eval.Feature.pInOutput = inputs.output_color;
   eval.pInExposureTexture = inputs.exposure;
   if (params.pre_exposure != 0.f)
      eval.InPreExposure = params.pre_exposure;
   eval.InReset = params.reset ? 1 : 0;
   eval.InMVScaleX = state.settings.mvs_x_scale;
   eval.InMVScaleY = state.settings.mvs_y_scale;
   eval.InJitterOffsetX = params.jitter_x;
   eval.InJitterOffsetY = params.jitter_y;
   return NVSDK_NGX_SUCCEED(NGX_D3D12_EVALUATE_DLSS_EXT(list, state.feature, state.runtime_params, &eval));
#else
   return false;
#endif
}

int GetJitterPhases(const State& state)
{
   return DlssCommon::ComputeJitterPhases(state.settings);
}
} // namespace Dlss
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
