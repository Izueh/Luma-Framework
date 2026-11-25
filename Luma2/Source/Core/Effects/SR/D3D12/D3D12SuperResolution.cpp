#include "Effects/SR/D3D12/D3D12SuperResolution.hpp"

#include <array>
#include <format>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Effects/SR/D3D12/D3D12Dlss.hpp"
#include "Effects/SR/D3D12/D3D12FfxFsr.hpp"
#include "Effects/SR/D3D12/D3D12XeSS.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Settings/SettingsRegistry.hpp"
#if DEVELOPMENT
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/DebugDraw.hpp"
#endif

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
namespace {
// Tuple indices match SR::Type order: Dlss (0), Fsr3 (1), Fsr4 (2), XeSS (3)
using BackendTuple = std::tuple<Dlss::State, FfxFsr::State, FfxFsr::State, XeSS::State>;

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
   BackendTuple backends;
   std::array<Capabilities, TYPE_COUNT> capabilities = {};
   Type active_type = Type::None;
   bool drew_this_frame = false;
   bool force_reset = false;
   SettingsData last_settings;
   DrawParams last_draw;
   int last_jitter_phases = GetDefaultJitterPhases();
   Hooks::Lifetime::D3D12::Object uav_output_temp;
   D3D12_RESOURCE_DESC uav_output_desc = {};
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

void EnsureInitialized(DeviceState& state, ID3D12Device* device)
{
   if (state.initialized || state.released || !device)
      return;
   state.initialized = true;
   Dlss::Init(std::get<0>(state.backends), device);
   // Init FSR 4 before FSR 3 so AMD spoofing is active on the first ffxQuery into amd_fidelityfx_upscaler_dx12.dll
   FfxFsr::Init(std::get<2>(state.backends), device, true);
   FfxFsr::Init(std::get<1>(state.backends), device, false);
   XeSS::Init(std::get<3>(state.backends), device);
   RefreshCapabilities(state);
   state.active_type = ResolveActiveType(GetUserType(), state.capabilities);
   PublishState(state);
}

ID3D12Resource* EnsureUavOutput(DeviceState& state, ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* output_resource,
   bool& needs_copy_back)
{
   needs_copy_back = false;
   const D3D12_RESOURCE_DESC desc = output_resource->GetDesc();
   if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
      return output_resource;
   needs_copy_back = true;
   if (!state.uav_output_temp.object || state.uav_output_desc.Width != desc.Width || state.uav_output_desc.Height != desc.Height ||
      state.uav_output_desc.Format != desc.Format)
   {
      D3D12_RESOURCE_DESC uav_desc = desc;
      uav_desc.Flags = (uav_desc.Flags | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) & ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
      ComPtr<ID3D12Resource> created;
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &uav_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
             IID_PPV_ARGS(&created))))
         return nullptr;
      state.uav_output_desc = uav_desc;
      Hooks::Lifetime::D3D12::Replace(state.uav_output_temp, std::move(created));
   }
   return static_cast<ID3D12Resource*>(Hooks::Lifetime::D3D12::Reference(list, state.uav_output_temp));
}

void TransitionIfNeeded(std::vector<D3D12_RESOURCE_BARRIER>& barriers, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   if (!resource || before == after)
      return;
   for (const D3D12_RESOURCE_BARRIER& existing : barriers)
   {
      if (existing.Transition.pResource == resource)
         return;
   }
   D3D12_RESOURCE_BARRIER barrier = {};
   barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
   barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
   barriers.push_back(barrier);
}
} // namespace

void CreateDeviceObjects(ID3D12Device* device)
{
   if (!IsNeeded() || !device)
      return;
   Hooks::Lifetime::SetState<DeviceState>(device);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return;
   std::scoped_lock lock(state->mutex);
   state->released = true;
   Hooks::Lifetime::D3D12::Replace(state->uav_output_temp, nullptr);
   Dlss::Deinit(std::get<0>(state->backends));
   FfxFsr::Deinit(std::get<1>(state->backends));
   FfxFsr::Deinit(std::get<2>(state->backends));
   XeSS::Deinit(std::get<3>(state->backends));
}

void OnPresent(ID3D12Device* device)
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

bool Draw(ID3D12GraphicsCommandList* list, const DrawOverrides& overrides)
{
   if (!IsNeeded() || !Settings::IsEnabled() || !list)
      return false;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return false;
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device.Get());
   if (!state)
      return false;

   std::scoped_lock lock(state->mutex);
   if (state->released)
      return false;
   EnsureInitialized(*state, device.Get());
   if (state->active_type == Type::None)
      return false;

   DrawInputs<ID3D12Resource> inputs;
   inputs.source_color = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::COLOR);
   inputs.depth_buffer = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::DEPTH);
   inputs.motion_vectors = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::MOTION_VECTORS);
   inputs.output_color = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::OUTPUT);
   inputs.exposure = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::EXPOSURE);
   inputs.bias_mask = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::REACTIVE);
   inputs.transparency_alpha = Resources::Database::D3D12::ReferenceResource(list, Resources::Database::Sr::TRANSPARENCY);
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

   ID3D12Resource* const final_output = inputs.output_color;
   bool needs_copy_back = false;
   inputs.output_color = EnsureUavOutput(*state, device.Get(), list, final_output, needs_copy_back);
   if (!inputs.output_color)
      return false;

   // In TAA's graphics draw, SRVs are in PIXEL_SHADER_RESOURCE and RTV0 is in RENDER_TARGET.
   // Compute SR backends require NON_PIXEL_SHADER_RESOURCE for inputs and UNORDERED_ACCESS for output.
   std::vector<D3D12_RESOURCE_BARRIER> pre_barriers;
   pre_barriers.reserve(7);
   TransitionIfNeeded(pre_barriers, inputs.source_color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   TransitionIfNeeded(pre_barriers, inputs.depth_buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   TransitionIfNeeded(pre_barriers, inputs.motion_vectors, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if (inputs.exposure)
      TransitionIfNeeded(pre_barriers, inputs.exposure, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if (inputs.bias_mask)
      TransitionIfNeeded(pre_barriers, inputs.bias_mask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if (inputs.transparency_alpha)
      TransitionIfNeeded(pre_barriers, inputs.transparency_alpha, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if (!needs_copy_back)
      TransitionIfNeeded(pre_barriers, final_output, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

   Hooks::Scope::Guard scope;
   if (!pre_barriers.empty())
      list->ResourceBarrier(UINT(pre_barriers.size()), pre_barriers.data());

   bool ok = VisitBackend(state->backends, state->active_type, [&](auto& backend) {
      if (!UpdateSettings(backend, list, settings))
         return false;
      state->last_jitter_phases = GetJitterPhases(backend);
      return Evaluate(backend, list, inputs, params);
   });

   if (ok && needs_copy_back)
   {
      D3D12_RESOURCE_BARRIER copy_pre[2] = {};
      copy_pre[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      copy_pre[0].Transition = {inputs.output_color, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
      copy_pre[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      copy_pre[1].Transition = {final_output, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST};
      list->ResourceBarrier(2, copy_pre);
      list->CopyResource(final_output, inputs.output_color);
      for (auto& b : copy_pre)
         std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
      list->ResourceBarrier(2, copy_pre);
   }

   for (auto& b : pre_barriers)
      std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
   if (!pre_barriers.empty())
      list->ResourceBarrier(UINT(pre_barriers.size()), pre_barriers.data());

   // Restore the game's descriptor heaps, root signatures, root arguments, PSO, and RTVs/DSV
   Resources::Database::D3D12::RestoreListState(list);

   if (ok)
   {
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
      Trace::DebugDraw::CaptureSrPassD3D12(list, std::format("Super Resolution ({})", GetTypeName(state->active_type)), inputs.source_color,
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
      Trace::Capture::D3D12::AddSrPass(list, sr_info, inputs.source_color, inputs.depth_buffer, inputs.motion_vectors, inputs.exposure, inputs.bias_mask,
         inputs.transparency_alpha, final_output);
   }
#endif
   return ok;
}

Type GetActiveType(ID3D12Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return Type::None;
   std::scoped_lock lock(state->mutex);
   return state->active_type;
}

int GetJitterPhases(ID3D12Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return GetDefaultJitterPhases();
   std::scoped_lock lock(state->mutex);
   return state->last_jitter_phases;
}
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
