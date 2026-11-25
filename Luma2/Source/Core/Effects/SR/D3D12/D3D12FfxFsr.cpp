#include "Effects/SR/D3D12/D3D12FfxFsr.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <dxgi1_4.h>

#include "Effects/SR/DXGI/DXGIFidelityFXSpoofing.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"

#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
namespace FfxFsr {
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
namespace {
constexpr UINT VENDOR_ID_AMD = 0x1002;
constexpr const wchar_t* FFX_LOADER_DLL = L"amd_fidelityfx_loader_dx12.dll";
constexpr const wchar_t* FFX_UPSCALER_DLL = L"amd_fidelityfx_upscaler_dx12.dll";

void FfxMessageCallback(uint32_t type, const wchar_t* message)
{
#if !DEVELOPMENT && !TEST
   if (type != FFX_API_MESSAGE_TYPE_ERROR)
      return;
#endif
   char buffer[512] = {};
   size_t converted = 0;
   wcstombs_s(&converted, buffer, message ? message : L"", sizeof(buffer) - 1);
   const std::string_view text(buffer);
   // Exposure is optional; FFX emits this warning every frame when auto-exposure is off and no exposure texture is bound.
   if (type != FFX_API_MESSAGE_TYPE_ERROR && text.find("exposure resource is null") != std::string_view::npos)
      return;
   static std::mutex seen_mutex;
   static std::unordered_set<std::string> seen;
   {
      std::scoped_lock lock(seen_mutex);
      if (!seen.emplace(text).second)
         return;
   }
   if (type == FFX_API_MESSAGE_TYPE_ERROR)
      Log::Error("SR D3D12 FSR: {}", text);
   else
      Log::Warning("SR D3D12 FSR: {}", text);
}

struct RetiredContext
{
   void* context = nullptr;
   void* fn_destroy = nullptr;
};

ComPtr<IDXGIAdapter> FindAdapterByLuid(LUID luid)
{
   ComPtr<IDXGIFactory4> factory;
   if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
      return nullptr;
   ComPtr<IDXGIAdapter1> adapter;
   if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
      return nullptr;
   return adapter;
}

void DestroyContext(State& state)
{
   state.context = nullptr;
   Hooks::Lifetime::D3D12::Replace(state.context_lifetime, nullptr);
}

bool SelectVersion(State& state, std::string& out_name, uint64_t& out_id)
{
   auto query_fn = reinterpret_cast<PfnFfxQuery>(state.fn_query);
   uint64_t count = 0;
   ffxQueryDescGetVersions desc = {};
   desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
   desc.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
   desc.device = state.device.Get();
   desc.outputCount = &count;
   std::vector<uint64_t> ids;
   std::vector<const char*> names;
   const bool spoofing = state.want_fsr4 && state.spoof_amd && DXGI::FidelityFXSpoofing::Begin(state.upscaler_module);
   if (query_fn(nullptr, &desc.header) == FFX_API_RETURN_OK && count > 0)
   {
      ids.resize(count);
      names.resize(count);
      desc.versionIds = ids.data();
      desc.versionNames = names.data();
      if (query_fn(nullptr, &desc.header) != FFX_API_RETURN_OK)
         names.clear();
   }
   if (spoofing)
      DXGI::FidelityFXSpoofing::End();

   const char want_major = state.want_fsr4 ? '4' : '3';
   for (size_t i = 0; i < names.size(); i++)
   {
      if (names[i] && names[i][0] == want_major && names[i][1] == '.')
      {
         out_name = names[i];
         out_id = ids[i];
         return true;
      }
   }
   return false;
}
} // namespace
#endif

bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D12Device* device, bool want_fsr4)
{
   Deinit(state);
   state.want_fsr4 = want_fsr4;
   state.capabilities.initialized = true;
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
   if (!device)
      return false;
   const std::filesystem::path folder = System::Modules::GetPath(System::Modules::GetSelf()).parent_path();
   HMODULE upscaler = LoadLibraryExW((folder / FFX_UPSCALER_DLL).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
   HMODULE loader = upscaler ? LoadLibraryExW((folder / FFX_LOADER_DLL).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
   if (!loader)
   {
      if (upscaler)
         FreeLibrary(upscaler);
      return false;
   }
   Hooks::Scope::AddIgnoredModule(upscaler);
   Hooks::Scope::AddIgnoredModule(loader);
   state.upscaler_module = upscaler;
   state.loader_module = loader;
   state.device = device;
   state.fn_create_context = reinterpret_cast<void*>(GetProcAddress(loader, "ffxCreateContext"));
   state.fn_destroy_context = reinterpret_cast<void*>(GetProcAddress(loader, "ffxDestroyContext"));
   state.fn_configure = reinterpret_cast<void*>(GetProcAddress(loader, "ffxConfigure"));
   state.fn_query = reinterpret_cast<void*>(GetProcAddress(loader, "ffxQuery"));
   state.fn_dispatch = reinterpret_cast<void*>(GetProcAddress(loader, "ffxDispatch"));
   if (!state.fn_create_context || !state.fn_destroy_context || !state.fn_query || !state.fn_dispatch)
   {
      Deinit(state);
      return false;
   }

   Hooks::Scope::Guard scope;
   const ComPtr<IDXGIAdapter> adapter = FindAdapterByLuid(device->GetAdapterLuid());
   DXGI_ADAPTER_DESC adapter_desc = {};
   const bool is_amd = adapter && SUCCEEDED(adapter->GetDesc(&adapter_desc)) && adapter_desc.VendorId == VENDOR_ID_AMD;
   state.capabilities.auto_selectable = !want_fsr4 || is_amd;
   if (want_fsr4 && !is_amd && adapter)
      state.spoof_amd = DXGI::FidelityFXSpoofing::Init(adapter.Get());

   std::string version_name;
   uint64_t version_id = 0;
   if (!SelectVersion(state, version_name, version_id))
      return false;
   state.version_id = version_id;
   state.capabilities.supported = true;
   state.capabilities.min_resolution = 32;
   state.capabilities.provider_version = version_name;
   Log::Info("SR: D3D12 FSR {} initialized{}", version_name, (want_fsr4 && state.spoof_amd) ? " (AMD GPU spoofed)" : "");
   return true;
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
   DestroyContext(state);
#endif
}

void Deinit([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
   DestroyContext(state);
   if (state.loader_module)
   {
      Hooks::Scope::RemoveIgnoredModule(state.loader_module);
      FreeLibrary(state.loader_module);
      state.loader_module = nullptr;
   }
   if (state.upscaler_module)
   {
      Hooks::Scope::RemoveIgnoredModule(state.upscaler_module);
      FreeLibrary(state.upscaler_module);
      state.upscaler_module = nullptr;
   }
#endif
   state.device.Reset();
   state.default_exposure.Reset();
   state.default_exposure_rtv.Reset();
   state.fn_create_context = nullptr;
   state.fn_destroy_context = nullptr;
   state.fn_configure = nullptr;
   state.fn_query = nullptr;
   state.fn_dispatch = nullptr;
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
   if (!list || !state.capabilities.supported || !state.device)
      return false;
   if (state.context && state.settings == settings)
      return true;

   Hooks::Scope::Guard scope;
   DestroyContext(state);

   ffxCreateContextDescUpscale desc = {};
   desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
   desc.maxUpscaleSize = {settings.output_width, settings.output_height};
   desc.maxRenderSize = settings.dynamic_resolution ? FfxApiDimensions2D{settings.output_width, settings.output_height}
                                                    : FfxApiDimensions2D{settings.render_width, settings.render_height};
   if (settings.auto_exposure)
      desc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
   if (settings.hdr)
      desc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
   if (settings.dynamic_resolution)
      desc.flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
   if (settings.inverted_depth)
      desc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
   if (settings.mvs_jittered)
      desc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
#if DEVELOPMENT
   desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;
#endif
   desc.fpMessage = FfxMessageCallback;

   ffxCreateBackendDX12Desc backend = {};
   backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
   backend.device = state.device.Get();
   desc.header.pNext = &backend.header;

   ffxCreateContextDescUpscaleVersion api_ver = {};
   api_ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
   api_ver.version = FFX_UPSCALER_VERSION;
   backend.header.pNext = &api_ver.header;

   ffxOverrideVersion override_ver = {};
   override_ver.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
   override_ver.versionId = state.version_id;
   if (state.version_id != 0)
      api_ver.header.pNext = &override_ver.header;

   auto create_fn = reinterpret_cast<PfnFfxCreateContext>(state.fn_create_context);
   const bool spoofing = state.want_fsr4 && state.spoof_amd && DXGI::FidelityFXSpoofing::Begin(state.upscaler_module);
   const ffxReturnCode_t ret = create_fn(&state.context, &desc.header, nullptr);
   if (spoofing)
      DXGI::FidelityFXSpoofing::End();
   if (ret != FFX_API_RETURN_OK)
   {
      if (state.logged_errors++ < 10)
         Log::Error("SR: D3D12 FSR ffxCreateContext failed ({}x{} -> {}x{}), err = {}", settings.render_width, settings.render_height, settings.output_width,
            settings.output_height, ret);
      state.context = nullptr;
      return false;
   }

   auto* retired = new RetiredContext{state.context, state.fn_destroy_context};
   ComPtr<IUnknown> holder;
   holder.Attach(Hooks::Lifetime::CreateStateHolder(retired, [](void* p) {
      auto* r = static_cast<RetiredContext*>(p);
      if (r->context && r->fn_destroy)
      {
         Hooks::Scope::Guard scope;
         reinterpret_cast<PfnFfxDestroyContext>(r->fn_destroy)(&r->context, nullptr);
      }
      delete r;
   }));
   Hooks::Lifetime::D3D12::Replace(state.context_lifetime, std::move(holder));

   auto query_fn = reinterpret_cast<PfnFfxQuery>(state.fn_query);
   state.phase_count = 16;
   ffxQueryDescUpscaleGetJitterPhaseCount phase = {};
   phase.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
   phase.renderWidth = settings.render_width;
   phase.displayWidth = settings.output_width;
   phase.pOutPhaseCount = &state.phase_count;
   query_fn(&state.context, &phase.header);

   state.settings = settings;
   return true;
#else
   return false;
#endif
}

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D12GraphicsCommandList* list, [[maybe_unused]] const DrawInputs<ID3D12Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4
   if (!state.capabilities.supported || !state.context || !list || !inputs.source_color || !inputs.output_color || !inputs.depth_buffer ||
      !inputs.motion_vectors)
      return false;
   Hooks::Lifetime::D3D12::Reference(list, state.context_lifetime);
   Hooks::Scope::Guard scope;
   if (!state.want_fsr4 && state.fn_configure)
   {
      float velocity = params.fsr_velocity_factor;
      ffxConfigureDescUpscaleKeyValue kv = {};
      kv.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
      kv.key = FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR;
      kv.ptr = &velocity;
      reinterpret_cast<PfnFfxConfigure>(state.fn_configure)(&state.context, &kv.header);
   }

   ffxDispatchDescUpscale dispatch = {};
   dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
   dispatch.commandList = list;
   dispatch.color = ffxApiGetResourceDX12(inputs.source_color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   dispatch.depth = ffxApiGetResourceDX12(inputs.depth_buffer, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   dispatch.motionVectors = ffxApiGetResourceDX12(inputs.motion_vectors, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   ID3D12Resource* exposure_resource = inputs.exposure;
   // FFX 4.x requires a non-null exposure texture when FFX_UPSCALE_ENABLE_AUTO_EXPOSURE is unset (returns error 6 otherwise).
   // Provide a 1x1 R32_FLOAT texture containing 1.0f (neutral exposure) so games without an exposure texture work without forcing auto-exposure.
   if (!exposure_resource && !state.settings.auto_exposure && state.device)
   {
      if (!state.default_exposure)
      {
         const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
         const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
         const D3D12_CLEAR_VALUE clear_val = {.Format = DXGI_FORMAT_R32_FLOAT, .Color = {1.f, 1.f, 1.f, 1.f}};
         const D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
         if (SUCCEEDED(state.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_val,
                IID_PPV_ARGS(&state.default_exposure))) &&
            SUCCEEDED(state.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&state.default_exposure_rtv))))
         {
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = state.default_exposure_rtv->GetCPUDescriptorHandleForHeapStart();
            state.device->CreateRenderTargetView(state.default_exposure.Get(), nullptr, rtv);
            const FLOAT one[4] = {1.f, 1.f, 1.f, 1.f};
            list->ClearRenderTargetView(rtv, one, 0, nullptr);
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {state.default_exposure.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
            list->ResourceBarrier(1, &barrier);
         }
         else
         {
            state.default_exposure.Reset();
            state.default_exposure_rtv.Reset();
         }
      }
      exposure_resource = state.default_exposure.Get();
   }
   if (exposure_resource)
      dispatch.exposure = ffxApiGetResourceDX12(exposure_resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   if (inputs.bias_mask)
      dispatch.reactive = ffxApiGetResourceDX12(inputs.bias_mask, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   if (inputs.transparency_alpha)
      dispatch.transparencyAndComposition = ffxApiGetResourceDX12(inputs.transparency_alpha, FFX_API_RESOURCE_STATE_COMPUTE_READ);
   dispatch.output = ffxApiGetResourceDX12(inputs.output_color, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

   dispatch.upscaleSize = {state.settings.output_width, state.settings.output_height};
   dispatch.renderSize = {params.render_width ? params.render_width : state.settings.render_width,
      params.render_height ? params.render_height : state.settings.render_height};
   dispatch.jitterOffset = {params.jitter_x, params.jitter_y};
   dispatch.motionVectorScale = {state.settings.mvs_x_scale, state.settings.mvs_y_scale};
   dispatch.preExposure = params.pre_exposure == 0.f ? 1.f : params.pre_exposure;
   dispatch.cameraFovAngleVertical = params.vert_fov;
   dispatch.cameraNear = state.settings.inverted_depth ? params.far_plane : params.near_plane;
   dispatch.cameraFar = state.settings.inverted_depth ? params.near_plane : params.far_plane;
   dispatch.viewSpaceToMetersFactor = 1.f;
   dispatch.reset = params.reset;
   dispatch.enableSharpening = params.user_sharpness >= 0.f;
   dispatch.sharpness = dispatch.enableSharpening ? params.user_sharpness : 0.f;
   dispatch.frameTimeDelta = (params.time_delta > 0.f ? params.time_delta : (1.f / 60.f)) * 1000.f;
#if DEVELOPMENT
   if (params.debug_view)
      dispatch.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
#endif
   const ffxReturnCode_t ret = reinterpret_cast<PfnFfxDispatch>(state.fn_dispatch)(&state.context, &dispatch.header);
   if (ret != FFX_API_RETURN_OK && state.logged_errors++ < 10)
      Log::Error("SR: D3D12 FSR ffxDispatch failed, err = {}", ret);
   return ret == FFX_API_RETURN_OK;
#else
   return false;
#endif
}

int GetJitterPhases(const State& state)
{
   return state.phase_count;
}
} // namespace FfxFsr
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
