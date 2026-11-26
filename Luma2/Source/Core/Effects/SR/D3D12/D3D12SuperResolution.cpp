#include "Effects/SR/D3D12/D3D12SuperResolution.hpp"

#include <array>
#include <format>
#include <mutex>
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
// FSR 3 and FSR 4 are both FidelityFX: two instances of one state type
struct Backends
{
   Dlss::State dlss;
   FfxFsr::State fsr3;
   FfxFsr::State fsr4;
   XeSS::State xess;
};

// False for no provider
template <typename Fn>
bool VisitBackend(Backends& backends, Type type, Fn&& fn)
{
   switch (type)
   {
   case Type::Dlss: return fn(backends.dlss);
   case Type::Fsr3: return fn(backends.fsr3);
   case Type::Fsr4: return fn(backends.fsr4);
   case Type::XeSS: return fn(backends.xess);
   case Type::None:
   case Type::Count: return false;
   }
   return false;
}

struct DeviceState
{
   std::mutex mutex;
   bool initialized = false;
   bool released = false;
   Backends backends;
   Record record;
   Hooks::Lifetime::D3D12::Object uav_output_temp;
   D3D12_RESOURCE_DESC uav_output_desc = {};
};

void RefreshCapabilities(DeviceState& state)
{
   state.record.capabilities[size_t(Type::Dlss)] = state.backends.dlss.capabilities;
   state.record.capabilities[size_t(Type::Fsr3)] = state.backends.fsr3.capabilities;
   state.record.capabilities[size_t(Type::Fsr4)] = state.backends.fsr4.capabilities;
   state.record.capabilities[size_t(Type::XeSS)] = state.backends.xess.capabilities;
}

void EnsureInitialized(DeviceState& state, ID3D12Device* device)
{
   if (state.initialized || state.released || !device)
      return;
   state.initialized = true;
   Dlss::Init(state.backends.dlss, device);
   // Init FSR 4 before FSR 3 so AMD spoofing is active on the first ffxQuery into amd_fidelityfx_upscaler_dx12.dll
   FfxFsr::Init(state.backends.fsr4, device, true);
   FfxFsr::Init(state.backends.fsr3, device, false);
   XeSS::Init(state.backends.xess, device);
   RefreshCapabilities(state);
   ChooseActiveType(state.record);
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
   Dlss::Deinit(state->backends.dlss);
   FfxFsr::Deinit(state->backends.fsr3);
   FfxFsr::Deinit(state->backends.fsr4);
   XeSS::Deinit(state->backends.xess);
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
   UpdateAtPresent(state->record, device);
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
   Record& record = state->record;
   if (record.active_type == Type::None)
      return false;

   DrawInputs<ID3D12Resource> inputs = GatherInputs<ID3D12Resource>([&](uint64_t key) { return Resources::Database::D3D12::ReferenceResource(list, key); });
   if (!HasRequiredInputs(inputs))
      return false;
   const std::optional<Prepared> prepared = PrepareDraw(record, device.Get(), overrides);
   if (!prepared)
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

   const bool ok = VisitBackend(state->backends, record.active_type, [&](auto& backend) {
      if (!UpdateSettings(backend, list, prepared->settings))
         return false;
      record.last_jitter_phases = GetJitterPhases(backend);
      return Evaluate(backend, list, inputs, prepared->params);
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

   FinishDraw(record, device.Get(), *prepared, ok);
#if DEVELOPMENT
   if (Trace::DebugDraw::IsActive())
   {
      Trace::DebugDraw::CaptureSrPassD3D12(list, std::format("Super Resolution ({})", GetTypeName(record.active_type)), inputs.source_color,
         inputs.depth_buffer, inputs.motion_vectors, inputs.exposure, inputs.bias_mask, inputs.transparency_alpha, final_output);
   }
   if (Trace::Capture::IsCapturing())
   {
      Trace::Capture::D3D12::AddSrPass(list, DescribeDraw(record, *prepared, ok, needs_copy_back), inputs.source_color, inputs.depth_buffer,
         inputs.motion_vectors, inputs.exposure, inputs.bias_mask, inputs.transparency_alpha, final_output);
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
   return state->record.active_type;
}

int GetJitterPhases(ID3D12Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return GetDefaultJitterPhases();
   std::scoped_lock lock(state->mutex);
   return state->record.last_jitter_phases;
}
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
