#include "Effects/SR/D3D11/D3D11Fsr3.hpp"

#include <cstdlib>
#include <cstring>

#include <wrl/client.h>

#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"

#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace Fsr3 {
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
namespace {
FfxResource MakeResource(ID3D11Resource* resource, FfxResourceDescription desc, const wchar_t* name)
{
   FfxResource out = {};
   out.resource = resource;
   out.state = FFX_RESOURCE_STATE_COMPUTE_READ;
   out.description = desc;
#if DEVELOPMENT
   if (name)
      wcscpy_s(out.name, name);
#else
   (void)name;
#endif
   return out;
}
} // namespace
#endif

bool Init([[maybe_unused]] State& state, [[maybe_unused]] ID3D11Device* device)
{
   Deinit(state);
   state.capabilities.initialized = true;
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   if (!device || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
      return false;
   state.context_storage = new FfxFsr3Context();
   state.capabilities.supported = true;
   state.capabilities.auto_selectable = true;
   state.capabilities.min_resolution = 32;
   state.capabilities.provider_version = "FSR 3.1 DX11";
   Log::Info("SR: D3D11 FSR 3 initialized");
   return true;
#else
   return false;
#endif
}

void ReleaseResources([[maybe_unused]] State& state)
{
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   if (state.has_context && state.context_storage)
   {
      Hooks::Scope::Guard scope;
      ffxFsr3ContextDestroy(static_cast<FfxFsr3Context*>(state.context_storage));
      std::memset(state.context_storage, 0, sizeof(FfxFsr3Context));
      state.has_context = false;
   }
   if (state.scratch_buffer)
   {
      std::free(state.scratch_buffer);
      state.scratch_buffer = nullptr;
   }
#endif
}

void Deinit([[maybe_unused]] State& state)
{
   ReleaseResources(state);
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   delete static_cast<FfxFsr3Context*>(state.context_storage);
   state.context_storage = nullptr;
#endif
   state.capabilities = {};
   state.settings = {};
}

bool UpdateSettings([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   if (!context || !state.capabilities.supported || !state.context_storage)
      return false;
   if (state.has_context && state.settings == settings)
      return true;

   Hooks::Scope::Guard scope;
   ReleaseResources(state);

   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (!device)
      return false;

   const size_t scratch_size = ffxGetScratchMemorySizeDX11(1);
   void* scratch = std::calloc(scratch_size, 1);
   if (!scratch)
      return false;

   FfxFsr3ContextDescription desc = {};
   FfxErrorCode err = ffxGetInterfaceDX11(&desc.backendInterfaceUpscaling, ffxGetDeviceDX11(device.Get()), scratch, scratch_size, 1);
   if (err != FFX_OK)
   {
      std::free(scratch);
      Log::Error("SR: D3D11 FSR 3 ffxGetInterfaceDX11 failed ({})", int(err));
      return false;
   }

   desc.displaySize = {settings.output_width, settings.output_height};
   desc.maxUpscaleSize = {settings.output_width, settings.output_height};
   desc.maxRenderSize = settings.dynamic_resolution ? FfxDimensions2D{settings.output_width, settings.output_height}
                                                    : FfxDimensions2D{settings.render_width, settings.render_height};
   desc.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY;
   if (settings.auto_exposure)
      desc.flags |= FFX_FSR3_ENABLE_AUTO_EXPOSURE;
   if (settings.hdr)
      desc.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
   if (settings.dynamic_resolution)
      desc.flags |= FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
   if (settings.inverted_depth)
      desc.flags |= FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DEPTH_INFINITE;
   if (settings.mvs_jittered)
      desc.flags |= FFX_FSR3_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
   desc.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;

   err = ffxFsr3ContextCreate(static_cast<FfxFsr3Context*>(state.context_storage), &desc);
   if (err != FFX_OK)
   {
      std::free(scratch);
      Log::Error("SR: D3D11 FSR 3 ffxFsr3ContextCreate failed ({})", int(err));
      return false;
   }
   state.scratch_buffer = scratch;
   state.has_context = true;
   state.settings = settings;
   state.phase_count = ffxFsr3GetJitterPhaseCount(settings.render_width, settings.output_width);
   return true;
#else
   return false;
#endif
}

bool Evaluate([[maybe_unused]] State& state, [[maybe_unused]] ID3D11DeviceContext* context, [[maybe_unused]] const DrawInputs<ID3D11Resource>& inputs,
   [[maybe_unused]] const DrawParams& params)
{
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   if (!state.capabilities.supported || !state.has_context || !context || !inputs.source_color || !inputs.output_color || !inputs.depth_buffer ||
      !inputs.motion_vectors)
      return false;
   Hooks::Scope::Guard scope;
   auto* ffx_ctx = static_cast<FfxFsr3Context*>(state.context_storage);
   float velocity_factor = params.fsr_velocity_factor;
   ffxFsr3SetUpscalerConstant(ffx_ctx, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, &velocity_factor);

   FfxFsr3DispatchUpscaleDescription dispatch = {};
   dispatch.commandList = ffxGetCommandListDX11(context);
   dispatch.color = MakeResource(inputs.source_color, GetFfxResourceDescriptionDX11(inputs.source_color), L"FSR3_color");
   dispatch.upscaleOutput = MakeResource(inputs.output_color, GetFfxResourceDescriptionDX11(inputs.output_color), L"FSR3_output");
   dispatch.depth = MakeResource(inputs.depth_buffer, GetFfxResourceDescriptionDX11(inputs.depth_buffer), L"FSR3_depth");
   dispatch.motionVectors = MakeResource(inputs.motion_vectors, GetFfxResourceDescriptionDX11(inputs.motion_vectors), L"FSR3_mv");
   if (inputs.exposure)
      dispatch.exposure = MakeResource(inputs.exposure, GetFfxResourceDescriptionDX11(inputs.exposure), L"FSR3_exposure");
   if (inputs.bias_mask)
      dispatch.reactive = MakeResource(inputs.bias_mask, GetFfxResourceDescriptionDX11(inputs.bias_mask), L"FSR3_reactive");
   if (inputs.transparency_alpha)
      dispatch.transparencyAndComposition = MakeResource(inputs.transparency_alpha, GetFfxResourceDescriptionDX11(inputs.transparency_alpha), L"FSR3_transp");
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
   dispatch.frameID = params.frame_index;
#if DEVELOPMENT
   if (params.debug_view)
      dispatch.flags |= FFX_FSR3_UPSCALER_FLAG_DRAW_DEBUG_VIEW;
#endif
   return ffxFsr3ContextDispatchUpscale(ffx_ctx, &dispatch) == FFX_OK;
#else
   return false;
#endif
}

int GetJitterPhases(const State& state)
{
   return state.phase_count;
}
} // namespace Fsr3
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
