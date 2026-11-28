#include "Effects/SR/D3D11/D3D11SuperResolution.hpp"

#include <array>
#include <format>
#include <mutex>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Effects/SR/D3D11/D3D11Dlss.hpp"
#include "Effects/SR/D3D11/D3D11Fsr3.hpp"
#include "Effects/SR/D3D11/D3D11Fsr4.hpp"
#include "Effects/SR/D3D11/D3D11XeSS.hpp"
#include "Effects/SR/D3D11/D3D12Interop.hpp"
#include "FrameState/FrameState.hpp"
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
struct Backends
{
   Dlss::State dlss;
   Fsr3::State fsr3;
   Fsr4::State fsr4;
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
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   D3D12Interop::Context interop;
#endif
   Backends backends;
   Record record;
   ComPtr<ID3D11Texture2D> uav_output_temp;
   D3D11_TEXTURE2D_DESC uav_output_desc = {};
};

void RefreshCapabilities(DeviceState& state)
{
   state.record.capabilities[size_t(Type::Dlss)] = state.backends.dlss.capabilities;
   state.record.capabilities[size_t(Type::Fsr3)] = state.backends.fsr3.capabilities;
   state.record.capabilities[size_t(Type::Fsr4)] = state.backends.fsr4.capabilities;
   state.record.capabilities[size_t(Type::XeSS)] = state.backends.xess.capabilities;
}

void EnsureInitialized(DeviceState& state, ID3D11Device* device)
{
   if (state.initialized || state.released || !device)
      return;
   state.initialized = true;
   Dlss::Init(state.backends.dlss, device);
   Fsr3::Init(state.backends.fsr3, device);
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   Fsr4::Init(state.backends.fsr4, device, &state.interop);
   XeSS::Init(state.backends.xess, device, &state.interop);
#else
   Fsr4::Init(state.backends.fsr4, device, nullptr);
   XeSS::Init(state.backends.xess, device, nullptr);
#endif
   RefreshCapabilities(state);
   ChooseActiveType(state.record);
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

void UpgradeFeatureLevels([[maybe_unused]] const D3D_FEATURE_LEVEL*& feature_levels, [[maybe_unused]] UINT& feature_level_count)
{
#if defined(LUMA_USE_FSR) && LUMA_USE_FSR
   // Required by FSR 3 on DX11. Only adds 11.1 at the front, never removes the game's levels;
   // D3D11Hooks hides the raised level from the game via GetFeatureLevel.
   // A null list doesn't include 11.1.
   static constexpr D3D_FEATURE_LEVEL default_levels_11_1[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1,
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
   Dlss::Deinit(state->backends.dlss);
   Fsr3::Deinit(state->backends.fsr3);
   Fsr4::Deinit(state->backends.fsr4);
   XeSS::Deinit(state->backends.xess);
#if defined(LUMA_USE_D3D12_INTEROP) && LUMA_USE_D3D12_INTEROP
   if (state->interop.initialized && D3D12Interop::WaitForIdle(state->interop))
      D3D12Interop::Deinit(state->interop);
#endif
}

void UpdateAtPresent(ID3D11Device* device)
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
   Record& record = state->record;
   if (record.active_type == Type::None)
      return false;

   DrawInputs<ID3D11Resource> inputs = GatherInputs<ID3D11Resource>([&](uint64_t key) { return Resources::Database::D3D11::GetResource(device.Get(), key); });
   if (!HasRequiredInputs(inputs))
      return false;
   const std::optional<Prepared> prepared = PrepareDraw(record, device.Get(), overrides);
   if (!prepared)
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

   const bool ok = VisitBackend(state->backends, record.active_type, [&](auto& backend) {
      if (!UpdateSettings(backend, context, prepared->settings))
         return false;
      record.last_jitter_phases = GetJitterPhases(backend);
      return Evaluate(backend, context, inputs, prepared->params);
   });

   Luma::Draw::D3D11::Restore(context, gfx_stack);
   Luma::Draw::D3D11::Restore(context, cs_stack);

   if (ok && needs_copy_back)
      context->CopyResource(final_output, inputs.output_color);
   FinishDraw(record, device.Get(), *prepared, ok);
#if DEVELOPMENT
   if (Trace::DebugDraw::IsActive())
   {
      Trace::DebugDraw::CaptureSrPassD3D11(context, std::format("Super Resolution ({})", GetTypeName(record.active_type)), inputs.source_color,
         inputs.depth_buffer, inputs.motion_vectors, inputs.exposure, inputs.bias_mask, inputs.transparency_alpha, final_output);
   }
   if (Trace::Capture::IsCapturing())
   {
      Trace::Capture::D3D11::AddSrPass(context, DescribeDraw(record, *prepared, {.succeeded = ok, .copy_back = needs_copy_back}), inputs.source_color, inputs.depth_buffer,
         inputs.motion_vectors, inputs.exposure, inputs.bias_mask, inputs.transparency_alpha, final_output);
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
   return state->record.active_type;
}

int GetJitterPhases(ID3D11Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return GetDefaultJitterPhases();
   std::scoped_lock lock(state->mutex);
   return state->record.last_jitter_phases;
}
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
