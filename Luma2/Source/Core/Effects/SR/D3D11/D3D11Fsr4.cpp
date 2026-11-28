#include "Effects/SR/D3D11/D3D11Fsr4.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Effects/SR/D3D11/D3D12Interop.hpp"
#include "Effects/SR/DXGI/DXGIFidelityFXSpoofing.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"

#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#endif

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace Fsr4 {
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
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
      Log::Error("SR FSR4: {}", text);
   else
      Log::Warning("SR FSR4: {}", text);
}

void LogError(State& state, const std::string& message)
{
   if (state.logged_errors++ < 10)
      Log::Error("SR FSR4: {}", message);
}

void DestroyContext(State& state)
{
   if (state.context && state.fn_destroy_context)
   {
      Hooks::Scope::Guard scope;
      auto destroy_fn = reinterpret_cast<PfnFfxDestroyContext>(state.fn_destroy_context);
      destroy_fn(&state.context, nullptr);
      state.context = nullptr;
   }
}

std::string QueryFsr4Version(State& state)
{
   auto query_fn = reinterpret_cast<PfnFfxQuery>(state.fn_query);
   uint64_t count = 0;
   ffxQueryDescGetVersions desc = {};
   desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
   desc.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
   desc.device = state.interop->device_12.Get();
   desc.outputCount = &count;
   std::vector<uint64_t> ids;
   std::vector<const char*> names;
   const bool spoofing = state.spoof_amd && DXGI::FidelityFXSpoofing::Begin(state.upscaler_module);
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
   for (const char* name : names)
   {
      if (name && name[0] == '4' && name[1] == '.')
         return name;
   }
   return {};
}
} // namespace
#endif

bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D11Device* device, [[maybe_unused]] D3D12Interop::Context* interop)
{
   Deinit(state);
   state.capabilities.initialized = true;
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (!device || !interop)
      return false;
   const std::filesystem::path folder = System::Modules::GetPath(System::Modules::GetSelf()).parent_path();
   HMODULE upscaler = LoadLibraryExW((folder / FFX_UPSCALER_DLL).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
   HMODULE loader = upscaler ? LoadLibraryExW((folder / FFX_LOADER_DLL).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
   if (!loader)
   {
      if (upscaler)
         FreeLibrary(upscaler);
      Log::Info("SR: FidelityFX DX12 DLLs not found next to Luma, D3D11 FSR 4 unavailable");
      return false;
   }
   Hooks::Scope::AddIgnoredModule(upscaler);
   Hooks::Scope::AddIgnoredModule(loader);
   state.upscaler_module = upscaler;
   state.loader_module = loader;
   state.interop = interop;
   state.fn_create_context = reinterpret_cast<void*>(GetProcAddress(loader, "ffxCreateContext"));
   state.fn_destroy_context = reinterpret_cast<void*>(GetProcAddress(loader, "ffxDestroyContext"));
   state.fn_query = reinterpret_cast<void*>(GetProcAddress(loader, "ffxQuery"));
   state.fn_dispatch = reinterpret_cast<void*>(GetProcAddress(loader, "ffxDispatch"));
   if (!state.fn_create_context || !state.fn_destroy_context || !state.fn_query || !state.fn_dispatch)
   {
      Deinit(state);
      return false;
   }
   if (!D3D12Interop::Init(*interop, device))
   {
      Deinit(state);
      return false;
   }
   state.capabilities.auto_selectable = interop->adapter_desc.VendorId == VENDOR_ID_AMD;
   if (!state.capabilities.auto_selectable)
      state.spoof_amd = DXGI::FidelityFXSpoofing::Init(interop->adapter.Get());

   const std::string version = QueryFsr4Version(state);
   if (version.empty())
   {
      Log::Info("SR: no FSR 4 upscaler available on this GPU");
      return false;
   }
   state.capabilities.supported = true;
   state.capabilities.min_resolution = 32;
   state.capabilities.provider_version = version;
   Log::Info("SR: D3D11 FSR {} initialized{}", version, state.spoof_amd ? " (AMD GPU spoofed)" : "");
   return true;
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (state.context && state.interop && D3D12Interop::WaitForIdle(*state.interop))
      DestroyContext(state);
#endif
}

void Deinit([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (state.context)
   {
      if (state.interop && D3D12Interop::WaitForIdle(*state.interop))
         DestroyContext(state);
      else
         state.context = nullptr;
   }
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
   state.fn_create_context = nullptr;
   state.fn_destroy_context = nullptr;
   state.fn_query = nullptr;
   state.fn_dispatch = nullptr;
   state.interop = nullptr;
   state.default_exposure.Reset();
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (!context || !state.capabilities.supported || !state.interop)
      return false;
   if (state.context && state.settings == settings)
      return true;
   if (!D3D12Interop::WaitForIdle(*state.interop))
      return false;
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
   backend.device = state.interop->device_12.Get();
   desc.header.pNext = &backend.header;

   ffxCreateContextDescUpscaleVersion api_ver = {};
   api_ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
   api_ver.version = FFX_UPSCALER_VERSION;
   backend.header.pNext = &api_ver.header;

   Hooks::Scope::Guard scope;
   auto create_fn = reinterpret_cast<PfnFfxCreateContext>(state.fn_create_context);
   const bool spoofing = state.spoof_amd && DXGI::FidelityFXSpoofing::Begin(state.upscaler_module);
   const ffxReturnCode_t ret = create_fn(&state.context, &desc.header, nullptr);
   if (spoofing)
      DXGI::FidelityFXSpoofing::End();
   if (ret != FFX_API_RETURN_OK)
   {
      LogError(state, std::format("ffxCreateContext failed ({}x{} -> {}x{}), err = {}", settings.render_width, settings.render_height, settings.output_width, settings.output_height, ret));
      state.context = nullptr;
      return false;
   }

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

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const DrawInputs<ID3D11Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_FSR4) && LUMA_USE_FSR4 && defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   using Slot = D3D12Interop::TextureSlot;
   if (!state.capabilities.supported || !state.context || !state.interop || !inputs.source_color || !inputs.output_color || !inputs.depth_buffer ||
      !inputs.motion_vectors)
      return false;

   ID3D11Resource* exposure_source = inputs.exposure;
   if (!exposure_source && !state.settings.auto_exposure && state.interop->device_11)
   {
      if (!state.default_exposure)
      {
         const float one = 1.f;
         const D3D11_TEXTURE2D_DESC desc = {1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
         const D3D11_SUBRESOURCE_DATA init = {&one, sizeof(float), sizeof(float)};
         Hooks::Scope::Guard scope;
         state.interop->device_11->CreateTexture2D(&desc, &init, &state.default_exposure);
      }
      exposure_source = state.default_exposure.Get();
   }
   ID3D11Resource* const sources[size_t(Slot::Count)] = {
      inputs.source_color, inputs.depth_buffer, inputs.motion_vectors, exposure_source, inputs.bias_mask, inputs.transparency_alpha, inputs.output_color};
   for (size_t i = 0; i < size_t(Slot::Count); i++)
   {
      if (sources[i] &&
         !D3D12Interop::UpdateSharedTextureFrom(*state.interop, state.interop->textures[i], sources[i],
            {.is_depth = i == size_t(Slot::Depth), .unordered_access = i == size_t(Slot::Depth) || i == size_t(Slot::Output)}))
         return false;
   }

   if (!D3D12Interop::CopyDepth(*state.interop, state.interop->textures[size_t(Slot::Depth)], context, inputs.depth_buffer))
      return false;
   for (Slot s : {Slot::Color, Slot::MotionVectors, Slot::Exposure, Slot::Reactive, Slot::Transparency})
   {
      if (sources[size_t(s)])
         context->CopyResource(state.interop->textures[size_t(s)].resource_11.Get(), sources[size_t(s)]);
   }

   ID3D12GraphicsCommandList* const list_12 = D3D12Interop::BeginCommandList(*state.interop, context);
   if (!list_12)
      return false;

   ffxDispatchDescUpscale dispatch = {};
   dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
   dispatch.commandList = list_12;
   FfxApiResource* const ffx_res[size_t(Slot::Count)] = {
      &dispatch.color, &dispatch.depth, &dispatch.motionVectors, &dispatch.exposure, &dispatch.reactive, &dispatch.transparencyAndComposition, &dispatch.output};
   D3D12_RESOURCE_BARRIER barriers[size_t(Slot::Count)] = {};
   UINT barrier_count = 0;
   for (size_t i = 0; i < size_t(Slot::Count); i++)
   {
      if (!sources[i])
         continue;
      const bool is_output = i == size_t(Slot::Output);
      ID3D12Resource* const res = state.interop->textures[i].resource_12.Get();
      *ffx_res[i] = ffxApiGetResourceDX12(res, is_output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ);
      D3D12_RESOURCE_BARRIER& b = barriers[barrier_count++];
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition = {res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON,
         is_output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
   }

   Hooks::Scope::Guard scope;
   list_12->ResourceBarrier(barrier_count, barriers);

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

   auto dispatch_fn = reinterpret_cast<PfnFfxDispatch>(state.fn_dispatch);
   const ffxReturnCode_t ret = dispatch_fn(&state.context, &dispatch.header);
   if (ret != FFX_API_RETURN_OK)
      LogError(state, std::format("ffxDispatch failed, err = {}", ret));

   for (UINT i = 0; i < barrier_count; i++)
      std::swap(barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
   list_12->ResourceBarrier(barrier_count, barriers);

   if (!D3D12Interop::ExecuteCommandList(*state.interop, context))
   {
      if (state.interop->poisoned)
         state.capabilities.supported = false;
      return false;
   }
   if (ret != FFX_API_RETURN_OK)
      return false;
   context->CopyResource(inputs.output_color, state.interop->textures[size_t(Slot::Output)].resource_11.Get());
   return true;
#else
   return false;
#endif
}

int GetJitterPhases(const State& state)
{
   return state.phase_count;
}
} // namespace Fsr4
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
