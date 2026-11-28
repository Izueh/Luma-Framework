#include "Resources/ResourceScaling.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <mutex>

#include <Windows.h>

#include "FrameState/FrameState.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Utils/Debug.hpp"

namespace Luma {
namespace Resources {
namespace Scaling {
namespace {
Options options;
std::atomic<bool> enabled = true;
std::atomic<uint32_t> scaled_mirrors = 0;
std::atomic<int32_t> pool_size = 0;
std::atomic<uint32_t> invalidations = 0;
std::atomic<uint32_t> viewport_rescales = 0;
std::atomic<uint32_t> scissor_rescales = 0;
std::atomic<uint32_t> copy_box_rescales = 0;
std::atomic<uint32_t> clear_rect_rescales = 0;
std::atomic<uint32_t> scaled_blits = 0;
std::atomic<uint32_t> cross_thread_overlaps = 0;
// Most recent device's active state for GetStats(nullptr)
std::atomic<uint32_t> last_active_thread = 0;

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::SCALING.key,
      .label = "Resolution Scaling",
      .tooltip = "Scales eligible mirrors, viewports, scissors and copies when early upscaling or custom dimensions are active.",
      .category = "Resources",
      .visibility = Settings::Visibility::Development,
      .default_value = true,
   },
};

// Attached to each device via ObjectLifetime. Never held with another lock.
struct DeviceState
{
   std::atomic<uint32_t> active_thread_id = 0;
   std::mutex mutex;
   Math::uint2 target_resolution = {};
   Math::uint2 last_render = {};
   Math::uint2 last_output = {};
};

DeviceState* GetOrCreateDeviceState(IUnknown* device)
{
   if (!device)
      return nullptr;
   IUnknown* const unwrapped = Hooks::DeviceProxy::Unwrap(device);
   if (DeviceState* existing = Hooks::Lifetime::GetStateOf<DeviceState>(unwrapped))
      return existing;
   return Hooks::Lifetime::SetStateOf<DeviceState>(unwrapped);
}

DeviceState* FindDeviceState(IUnknown* device)
{
   return device ? Hooks::Lifetime::GetStateOf<DeviceState>(Hooks::DeviceProxy::Unwrap(device)) : nullptr;
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::SCALING.key)
         enabled.store(std::get<bool>(value), std::memory_order_relaxed);
   });
}

void SetOptions(const Options& value)
{
   options = value;
   if (options.stencil_stand_in)
      LUMA_UNIMPLEMENTED("Resources::Scaling stencil stand-ins");
   if (options.pool_by_frame)
      LUMA_UNIMPLEMENTED("Resources::Scaling per-frame target pooling");
}

const Options& GetOptions()
{
   return options;
}

bool IsNeeded()
{
   return options.mode != Mode::None || !Upgrade::GetRules().custom_dimensions.empty() || Chains::HasScaleSeeds();
}

bool IsEnabled()
{
   return Settings::IsEnabled() && enabled.load(std::memory_order_relaxed);
}

std::string_view GetModeName(Mode mode)
{
   switch (mode)
   {
   case Mode::ResourcesAndViewports: return "Resources & Viewports";
   case Mode::ViewportsOnly: return "Viewports Only";
   default: return "None";
   }
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!IsNeeded())
      return functions;
   for (const Hooks::D3D11::Function function : {RSSetViewports, RSSetScissorRects, OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews, ClearState,
           FinishCommandList, ExecuteCommandList, Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect,
           DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect})
      functions.set(size_t(function));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   if (!IsNeeded())
      return functions;
   for (const Hooks::D3D12::Function function : {SetFixedState, OMSetRenderTargets, Reset, ClearState, Draw, Dispatch, ExecuteIndirect, SetPipelineState})
      functions.set(size_t(function));
   return functions;
}

void Begin(IUnknown* device)
{
   if (DeviceState* state = GetOrCreateDeviceState(device))
   {
      const uint32_t thread_id = GetCurrentThreadId();
      state->active_thread_id.store(thread_id, std::memory_order_release);
      last_active_thread.store(thread_id, std::memory_order_relaxed);
   }
}

void End(IUnknown* device)
{
   if (DeviceState* state = FindDeviceState(device))
   {
      state->active_thread_id.store(0, std::memory_order_release);
      last_active_thread.store(0, std::memory_order_relaxed);
   }
}

void ResetFrame(IUnknown* device)
{
   End(device);
}

bool IsActive(IUnknown* device)
{
   const DeviceState* state = FindDeviceState(device);
   return state && state->active_thread_id.load(std::memory_order_acquire) != 0;
}

bool IsActiveOnCurrentThread(IUnknown* device)
{
   const DeviceState* state = FindDeviceState(device);
   if (!state)
      return false;
   const uint32_t active = state->active_thread_id.load(std::memory_order_acquire);
   return active != 0 && active == GetCurrentThreadId();
}

bool CheckCrossThreadOverlap(IUnknown* device)
{
   const DeviceState* state = FindDeviceState(device);
   if (!state)
      return false;
   const uint32_t active = state->active_thread_id.load(std::memory_order_acquire);
   if (active != 0 && active != GetCurrentThreadId())
   {
      cross_thread_overlaps.fetch_add(1, std::memory_order_relaxed);
      return true;
   }
   return false;
}

void SetTargetResolution(IUnknown* device, Math::uint2 size)
{
   if (DeviceState* state = GetOrCreateDeviceState(device))
   {
      std::scoped_lock lock(state->mutex);
      state->target_resolution = size;
   }
}

Math::uint2 GetTargetResolution(IUnknown* device)
{
   if (DeviceState* state = FindDeviceState(device))
   {
      std::scoped_lock lock(state->mutex);
      if (state->target_resolution.x > 0 && state->target_resolution.y > 0)
         return state->target_resolution;
   }
   return FrameState::Get(device).output;
}

bool ShouldActivateAfter(uint32_t shader_hash)
{
   return std::ranges::find(options.activate_after_hashes, shader_hash) != options.activate_after_hashes.end();
}

bool ShouldDeactivateAt(uint32_t shader_hash)
{
   return std::ranges::find(options.deactivate_at_hashes, shader_hash) != options.deactivate_at_hashes.end();
}

bool ConsumeResolutionChange(IUnknown* device, Math::uint2 render, Math::uint2 output)
{
   DeviceState* state = GetOrCreateDeviceState(device);
   if (!state)
      return false;
   std::scoped_lock lock(state->mutex);
   const bool had_previous = state->last_output.x > 0 && state->last_output.y > 0 && state->last_render.x > 0 && state->last_render.y > 0;
   const bool changed = had_previous && (state->last_render != render || state->last_output != output);
   state->last_render = render;
   state->last_output = output;
   return changed;
}

bool ShouldScaleTexture(const Upgrade::TextureInfo& desc, const Upgrade::FrameState& state, ScaleContext context)
{
   if (!context.active || desc.dimension != Upgrade::Dimension::Texture2D)
      return false;
   if (state.render.x == 0 || state.render.y == 0 || state.output.x == 0 || state.output.y == 0)
      return false;
   if (state.render.x >= state.output.x || state.render.y >= state.output.y)
      return false;
   if (desc.width <= 1 && desc.height <= 1)
      return false;
   if (desc.width >= state.output.x || desc.height >= state.output.y)
      return false;
   if (!context.from_output_sized_source)
      return false;
   const float threshold = float(context.aspect_ratio_pixel_threshold);
   const float width = float(desc.width);
   const float height = float(desc.height);
   const float min_ratio = desc.width <= desc.height ? (width - threshold) / height : width / (height + threshold);
   const float max_ratio = desc.width <= desc.height ? (width + threshold) / height : width / (height - threshold);
   const float render_ratio = float(state.render.x) / float(state.render.y);
   return render_ratio >= min_ratio - FLT_EPSILON && render_ratio <= max_ratio + FLT_EPSILON;
}

Math::uint2 ComputeScaledSize(Math::uint2 original, Math::uint2 render, Math::uint2 output)
{
   if (original.x == 0 || original.y == 0 || render.x == 0 || render.y == 0 || output.x == 0 || output.y == 0)
      return original;
   if (original.x == render.x && original.y == render.y)
      return output;
   const uint32_t width = std::clamp(uint32_t(std::lround(double(original.x) * double(output.x) / double(render.x))), 1u, output.x);
   const uint32_t height = std::clamp(uint32_t(std::lround(double(original.y) * double(output.y) / double(render.y))), 1u, output.y);
   return {width, height};
}

Viewport RescaleViewport(const Viewport& viewport, Math::float2 scale)
{
   if (viewport.width == 0.f || viewport.height == 0.f)
      return viewport;
   // Scale edges and round to whole pixels so adjacent tiled viewports leave no gaps or overlaps
   const float left = std::round(viewport.x * scale.x);
   const float top = std::round(viewport.y * scale.y);
   const float right = std::round((viewport.x + viewport.width) * scale.x);
   const float bottom = std::round((viewport.y + viewport.height) * scale.y);
   return {left, top, std::max(right - left, 0.f), std::max(bottom - top, 0.f), viewport.min_depth, viewport.max_depth};
}

Rect RescaleRect(const Rect& rect, Math::float2 scale)
{
   return {
      int32_t(std::lround(double(rect.left) * double(scale.x))),
      int32_t(std::lround(double(rect.top) * double(scale.y))),
      int32_t(std::lround(double(rect.right) * double(scale.x))),
      int32_t(std::lround(double(rect.bottom) * double(scale.y))),
   };
}

Box RescaleBox(const Box& box, Math::float2 scale)
{
   const uint32_t left = uint32_t(std::max<long>(std::lround(double(box.left) * double(scale.x)), 0l));
   const uint32_t top = uint32_t(std::max<long>(std::lround(double(box.top) * double(scale.y)), 0l));
   const uint32_t right = uint32_t(std::max<long>(std::lround(double(box.right) * double(scale.x)), long(left)));
   const uint32_t bottom = uint32_t(std::max<long>(std::lround(double(box.bottom) * double(scale.y)), long(top)));
   return {left, top, box.front, right, bottom, box.back};
}

void CountScaledMirror(int32_t delta)
{
   scaled_mirrors.fetch_add(uint32_t(delta), std::memory_order_relaxed);
}

void CountPoolSize(int32_t delta)
{
   pool_size.fetch_add(delta, std::memory_order_relaxed);
}

void CountInvalidation()
{
   invalidations.fetch_add(1, std::memory_order_relaxed);
}

void CountViewportRescale()
{
   viewport_rescales.fetch_add(1, std::memory_order_relaxed);
}

void CountScissorRescale()
{
   scissor_rescales.fetch_add(1, std::memory_order_relaxed);
}

void CountCopyBoxRescale()
{
   copy_box_rescales.fetch_add(1, std::memory_order_relaxed);
}

void CountClearRectRescale()
{
   clear_rect_rescales.fetch_add(1, std::memory_order_relaxed);
}

void CountScaledBlit()
{
   scaled_blits.fetch_add(1, std::memory_order_relaxed);
}

Stats GetStats(IUnknown* device)
{
   const DeviceState* state = FindDeviceState(device);
   const uint32_t active_thread = state ? state->active_thread_id.load(std::memory_order_relaxed) : last_active_thread.load(std::memory_order_relaxed);
   return {
      .mode = options.mode,
      .active = active_thread != 0,
      .activating_thread = active_thread,
      .scaled_mirrors = scaled_mirrors.load(std::memory_order_relaxed),
      .pool_size = uint32_t(std::max<int32_t>(pool_size.load(std::memory_order_relaxed), 0)),
      .invalidations = invalidations.load(std::memory_order_relaxed),
      .viewport_rescales = viewport_rescales.load(std::memory_order_relaxed),
      .scissor_rescales = scissor_rescales.load(std::memory_order_relaxed),
      .copy_box_rescales = copy_box_rescales.load(std::memory_order_relaxed),
      .clear_rect_rescales = clear_rect_rescales.load(std::memory_order_relaxed),
      .scaled_blits = scaled_blits.load(std::memory_order_relaxed),
      .cross_thread_overlaps = cross_thread_overlaps.load(std::memory_order_relaxed),
   };
}
} // namespace Scaling
} // namespace Resources
} // namespace Luma
