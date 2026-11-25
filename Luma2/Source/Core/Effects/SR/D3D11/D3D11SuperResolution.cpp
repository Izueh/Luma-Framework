#include "Effects/SR/D3D11/D3D11SuperResolution.hpp"

#include <array>
#include <format>
#include <mutex>
#include <tuple>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Effects/SR/D3D11/D3D11Dlss.hpp"
#include "Effects/SR/D3D11/D3D11Fsr3.hpp"
#include "Effects/SR/D3D11/D3D11Fsr4.hpp"
#include "Effects/SR/D3D11/D3D11XeSS.hpp"
#include "Effects/SR/D3D11/D3D12Interop.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Settings/SettingsRegistry.hpp"
#if DEVELOPMENT
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Trace/DebugDraw.hpp"
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace {
using BackendTuple = std::tuple<Dlss::State, Fsr3::State, Fsr4::State, XeSS::State>;

template <typename Fn>
decltype(auto) VisitBackend(BackendTuple& backends, Type type, Fn&& fn)
{
   switch (type)
   {
   case Type::Dlss: return fn(std::get<0>(backends));
   case Type::Fsr3: return fn(std::get<1>(backends));
   case Type::Fsr4: return fn(std::get<2>(backends));
   case Type::XeSS: return fn(std::get<3>(backends));
   default: return fn(std::get<0>(backends));
   }
}

struct DeviceState
{
   std::mutex mutex;
   bool initialized = false;
   bool released = false;
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   D3D12Interop::Context interop;
#endif
   BackendTuple backends;
   std::array<Capabilities, TYPE_COUNT> capabilities = {};
   Type active_type = Type::None;
   bool drew_this_frame = false;
   bool force_reset = false;
   SettingsData last_settings;
   DrawParams last_draw;
   int last_jitter_phases = GetDefaultJitterPhases();
   ComPtr<ID3D11Texture2D> uav_output_temp;
   D3D11_TEXTURE2D_DESC uav_output_desc = {};
};

void RefreshCapabilities(DeviceState& state)
{
   state.capabilities[size_t(Type::Dlss)] = std::get<0>(state.backends).capabilities;
   state.capabilities[size_t(Type::Fsr3)] = std::get<1>(state.backends).capabilities;
   state.capabilities[size_t(Type::Fsr4)] = std::get<2>(state.backends).capabilities;
   state.capabilities[size_t(Type::XeSS)] = std::get<3>(state.backends).capabilities;
}

void PublishState(const DeviceState& state)
{
   RuntimeInfo info;
   info.enabled = IsNeeded();
   info.user_type = GetUserType();
   info.active_type = state.active_type;
   info.drew_this_frame = state.drew_this_frame;
   info.backends = state.capabilities;
   info.settings = state.last_settings;
   info.last_draw = state.last_draw;
   info.jitter_phases = state.last_jitter_phases;
   PublishRuntimeInfo(info);
}

void EnsureInitialized(DeviceState& state, ID3D11Device* device)
{
   if (state.initialized || state.released || !device)
      return;
   state.initialized = true;
   Dlss::Init(std::get<0>(state.backends), device);
   Fsr3::Init(std::get<1>(state.backends), device);
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   Fsr4::Init(std::get<2>(state.backends), device, &state.interop);
   XeSS::Init(std::get<3>(state.backends), device, &state.interop);
#else
   Fsr4::Init(std::get<2>(state.backends), device, nullptr);
   XeSS::Init(std::get<3>(state.backends), device, nullptr);
#endif
   RefreshCapabilities(state);
   state.active_type = ResolveActiveType(GetUserType(), state.capabilities);
   PublishState(state);
}

ID3D11Resource* EnsureUavOutput(DeviceState& state, ID3D11Device* device, ID3D11Resource* output_resource, bool& needs_copy_back)
{
   needs_copy_back = false;
   ComPtr<ID3D11Texture2D> tex;
   if (FAILED(output_resource->QueryInterface(IID_PPV_ARGS(&tex))))
      return nullptr;
   D3D11_TEXTURE2D_DESC desc = {};
   tex->GetDesc(&desc);
   if ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
      return output_resource;
   needs_copy_back = true;
   desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
   if (!state.uav_output_temp || state.uav_output_desc.Width != desc.Width || state.uav_output_desc.Height != desc.Height ||
      state.uav_output_desc.Format != desc.Format)
   {
      state.uav_output_temp.Reset();
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &state.uav_output_temp)))
         return nullptr;
      state.uav_output_desc = desc;
   }
   return state.uav_output_temp.Get();
}
} // namespace

void PreCreateDevice([[maybe_unused]] const D3D_FEATURE_LEVEL*& feature_levels, [[maybe_unused]] UINT& feature_level_count)
{
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   // Required by FSR 3 on DX11. Only adds 11.1 at the front, never removes the game's levels;
   // D3D11Hooks hides the raised level from the game via GetFeatureLevel.
   // A null list doesn't include 11.1.
   static constexpr D3D_FEATURE_LEVEL default_levels_11_1[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1,
   };
   if (!feature_levels || feature_level_count == 0)
   {
      feature_levels = default_levels_11_1;
      feature_level_count = UINT(std::size(default_levels_11_1));
      return;
   }
   if (feature_levels[0] == D3D_FEATURE_LEVEL_11_0)
   {
      thread_local std::vector<D3D_FEATURE_LEVEL> levels;
      levels.assign(feature_levels, feature_levels + feature_level_count);
      levels.insert(levels.begin(), D3D_FEATURE_LEVEL_11_1);
      feature_levels = levels.data();
      feature_level_count = UINT(levels.size());
   }
#endif
}

void CreateDeviceObjects(ID3D11Device* device)
{
   if (!IsNeeded() || !device)
      return;
   Hooks::Lifetime::SetState<DeviceState>(device);
}

void ReleaseDeviceObjects(ID3D11Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return;
   std::scoped_lock lock(state->mutex);
   state->released = true;
   state->uav_output_temp.Reset();
   Dlss::Deinit(std::get<0>(state->backends));
   Fsr3::Deinit(std::get<1>(state->backends));
   Fsr4::Deinit(std::get<2>(state->backends));
   XeSS::Deinit(std::get<3>(state->backends));
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (state->interop.initialized && D3D12Interop::WaitForIdle(state->interop))
      D3D12Interop::Deinit(state->interop);
#endif
}

void OnPresent(ID3D11Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return;
   std::scoped_lock lock(state->mutex);
   if (state->released)
      return;
   EnsureInitialized(*state, device);
   const Type next_type = ResolveActiveType(GetUserType(), state->capabilities);
   if (next_type != state->active_type)
   {
      state->active_type = next_type;
      state->force_reset = true;
   }
   const uint32_t sr_type_value = uint32_t(state->active_type == Type::None ? -1 : int32_t(state->active_type));
   if (Resources::LumaCBuffers::cb_luma_global_settings.SRType != sr_type_value)
   {
      Resources::LumaCBuffers::cb_luma_global_settings.SRType = sr_type_value;
      Resources::LumaCBuffers::MarkSettingsDirty();
   }
   const Frame::Resolutions res = Frame::Get(device);
   UpdateSamplerLodBias(state->active_type, Frame::HasPass(device, Frame::PassMarker::Taa), res.render.y, res.output.y);
   PublishState(*state);
   state->drew_this_frame = false;
}

bool Draw(ID3D11DeviceContext* context, const DrawOverrides& overrides)
{
   if (!IsNeeded() || !Settings::IsEnabled() || !context)
      return false;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   DeviceState* state = device ? Hooks::Lifetime::GetState<DeviceState>(device.Get()) : nullptr;
   if (!state)
      return false;

   std::scoped_lock lock(state->mutex);
   if (state->released)
      return false;
   EnsureInitialized(*state, device.Get());
   if (state->active_type == Type::None)
      return false;

   DrawInputs<ID3D11Resource> inputs;
   inputs.source_color = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::COLOR);
   inputs.depth_buffer = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::DEPTH);
   inputs.motion_vectors = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::MOTION_VECTORS);
   inputs.output_color = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::OUTPUT);
   inputs.exposure = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::EXPOSURE);
   inputs.bias_mask = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::REACTIVE);
   inputs.transparency_alpha = Resources::Database::D3D11::GetResource(device.Get(), Resources::Database::Sr::TRANSPARENCY);
   if (!inputs.source_color || !inputs.depth_buffer || !inputs.motion_vectors || !inputs.output_color)
      return false;

   const Frame::Resolutions resolutions = Frame::Get(device.Get());
   const Frame::Camera camera = Frame::GetCamera(device.Get());
   const Frame::Timing timing = Frame::GetTiming(device.Get());
   const SettingsData settings = BuildSettingsData(GetOptions(), resolutions, overrides);
   DrawParams params = BuildDrawParams(resolutions, camera, timing, overrides);
   if (state->force_reset)
   {
      params.reset = true;
      state->force_reset = false;
   }

   const uint32_t min_res = state->capabilities[size_t(state->active_type)].min_resolution;
   if (settings.output_width < min_res || settings.output_height < min_res || params.render_width < min_res || params.render_height < min_res)
      return false;

   ID3D11Resource* const final_output = inputs.output_color;
   bool needs_copy_back = false;
   inputs.output_color = EnsureUavOutput(*state, device.Get(), final_output, needs_copy_back);
   if (!inputs.output_color)
      return false;

   Luma::Draw::D3D11::StateStack gfx_stack{.mask = Luma::Draw::FULL_GRAPHICS};
   Luma::Draw::D3D11::StateStack cs_stack{.mask = Luma::Draw::COMPUTE};
   Hooks::Scope::Guard scope;
   Luma::Draw::D3D11::Save(context, gfx_stack);
   Luma::Draw::D3D11::Save(context, cs_stack);

   bool ok = VisitBackend(state->backends, state->active_type, [&](auto& backend) {
      if (!UpdateSettings(backend, context, settings))
         return false;
      state->last_jitter_phases = GetJitterPhases(backend);
      return Evaluate(backend, context, inputs, params);
   });

   Luma::Draw::D3D11::Restore(context, gfx_stack);
   Luma::Draw::D3D11::Restore(context, cs_stack);

   if (ok)
   {
      if (needs_copy_back)
         context->CopyResource(final_output, inputs.output_color);
      state->drew_this_frame = true;
      state->last_settings = settings;
      state->last_draw = params;
      Frame::MarkPass(device.Get(), Frame::PassMarker::Sr);
      PublishState(*state);
   }
   else
   {
      state->force_reset = true;
   }
#if DEVELOPMENT
   if (Trace::DebugDraw::IsActive())
   {
      Trace::DebugDraw::CaptureSrPassD3D11(context, std::format("Super Resolution ({})", GetTypeName(state->active_type)), inputs.source_color,
         inputs.depth_buffer, inputs.motion_vectors, inputs.exposure, inputs.bias_mask, inputs.transparency_alpha, final_output);
   }
   if (Trace::Capture::IsCapturing())
   {
      const Capabilities& cap = state->capabilities[size_t(state->active_type)];
      Trace::Capture::SrInfo sr_info = {
         .backend = std::string(GetTypeName(state->active_type)),
         .provider_version = cap.provider_version,
         .render_width = params.render_width,
         .render_height = params.render_height,
         .output_width = settings.output_width,
         .output_height = settings.output_height,
         .dynamic_resolution = settings.dynamic_resolution,
         .hdr = settings.hdr,
         .inverted_depth = settings.inverted_depth,
         .auto_exposure = settings.auto_exposure,
         .mvs_jittered = settings.mvs_jittered,
         .reset = params.reset,
         .succeeded = ok,
         .copy_back = needs_copy_back,
         .jitter_x = params.jitter_x,
         .jitter_y = params.jitter_y,
         .jitter_phases = state->last_jitter_phases,
         .vert_fov = params.vert_fov,
         .near_plane = params.near_plane,
         .far_plane = params.far_plane,
         .pre_exposure = params.pre_exposure,
         .mvs_x_scale = settings.mvs_x_scale,
         .mvs_y_scale = settings.mvs_y_scale,
         .sharpness = params.user_sharpness,
         .velocity_factor = params.fsr_velocity_factor,
      };
      Trace::Capture::D3D11::AddSrPass(context, sr_info, inputs.source_color, inputs.depth_buffer, inputs.motion_vectors, inputs.exposure, inputs.bias_mask,
         inputs.transparency_alpha, final_output);
   }
#endif
   return ok;
}

Type GetActiveType(ID3D11Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return Type::None;
   std::scoped_lock lock(state->mutex);
   return state->active_type;
}

int GetJitterPhases(ID3D11Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return GetDefaultJitterPhases();
   std::scoped_lock lock(state->mutex);
   return state->last_jitter_phases;
}
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
