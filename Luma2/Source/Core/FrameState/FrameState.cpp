#include "FrameState/FrameState.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include <Windows.h>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ObjectLifetime.hpp"

namespace Luma {
namespace FrameState {
// Read at every LumaData write and LumaSettings upload and by the game, written at swapchain changes and draws: a lock keeps fields
// consistent. Never held with another lock.
struct DeviceState
{
   mutable std::mutex mutex;
   Resolutions resolutions;
   Camera camera;
   Timing timing;
   std::array<bool, size_t(PassMarker::Count)> markers = {};
   std::vector<uint64_t> custom_markers;
   int64_t last_present_qpc = 0;

   explicit DeviceState(Math::uint2 size) : resolutions{size, size, size, size, size} {}
};

namespace {
std::atomic<uint64_t> generation = 0;

int64_t GetQpcFrequency()
{
   static const int64_t frequency = [] {
      LARGE_INTEGER value = {};
      QueryPerformanceFrequency(&value);
      return value.QuadPart > 0 ? value.QuadPart : 1;
   }();
   return frequency;
}

int64_t GetQpcNow()
{
   LARGE_INTEGER value = {};
   QueryPerformanceCounter(&value);
   return value.QuadPart;
}

// The screen's size, a better guess than a fixed 1920x1080, until the swapchain is created
Math::uint2 GetScreenSize()
{
   return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

DeviceState* FindMutable(IUnknown* device)
{
   return device ? Hooks::Lifetime::GetStateOf<DeviceState>(Hooks::DeviceProxy::Unwrap(device)) : nullptr;
}
} // namespace

void CreateDeviceState(IUnknown* device)
{
   Hooks::Lifetime::SetStateOf<DeviceState>(device, GetScreenSize());
}

const DeviceState* Find(IUnknown* device)
{
   return FindMutable(device);
}

Resolutions Get(IUnknown* device)
{
   return Read(Find(device));
}

Resolutions Read(const DeviceState* state)
{
   if (!state)
   {
      const Math::uint2 size = GetScreenSize();
      return {size, size, size, size, size};
   }
   std::scoped_lock lock(state->mutex);
   return state->resolutions;
}

void SetOutputResolution(IUnknown* device, Math::uint2 size)
{
   if (DeviceState* state = FindMutable(device))
   {
      {
         std::scoped_lock lock(state->mutex);
         state->resolutions = {size, size, size, size, state->resolutions.display};
      }
      generation.fetch_add(1, std::memory_order_release);
   }
}

void SetSwapchainResolution(IUnknown* device, Math::uint2 swapchain, Math::uint2 display)
{
   if (DeviceState* state = FindMutable(device))
   {
      {
         std::scoped_lock lock(state->mutex);
         state->resolutions.swapchain = swapchain;
         state->resolutions.display = display;
      }
      generation.fetch_add(1, std::memory_order_release);
   }
}

void SetRenderResolution(IUnknown* device, Math::uint2 size)
{
   if (DeviceState* state = FindMutable(device))
   {
      {
         std::scoped_lock lock(state->mutex);
         state->resolutions.previous_render = state->resolutions.render;
         state->resolutions.render = size;
      }
      generation.fetch_add(1, std::memory_order_release);
   }
}

uint64_t GetGeneration()
{
   return generation.load(std::memory_order_acquire);
}

void SetCamera(IUnknown* device, const Camera& camera)
{
   if (DeviceState* state = FindMutable(device))
   {
      std::scoped_lock lock(state->mutex);
      state->camera = camera;
   }
}

Camera GetCamera(IUnknown* device)
{
   if (const DeviceState* state = Find(device))
   {
      std::scoped_lock lock(state->mutex);
      return state->camera;
   }
   return {};
}

Timing GetTiming(IUnknown* device)
{
   if (const DeviceState* state = Find(device))
   {
      std::scoped_lock lock(state->mutex);
      return state->timing;
   }
   return {};
}

void MarkPass(IUnknown* device, PassMarker marker)
{
   if (size_t(marker) >= size_t(PassMarker::Count))
      return;
   if (DeviceState* state = FindMutable(device))
   {
      std::scoped_lock lock(state->mutex);
      state->markers[size_t(marker)] = true;
   }
}

bool HasPass(IUnknown* device, PassMarker marker)
{
   if (size_t(marker) >= size_t(PassMarker::Count))
      return false;
   if (const DeviceState* state = Find(device))
   {
      std::scoped_lock lock(state->mutex);
      return state->markers[size_t(marker)];
   }
   return false;
}

void MarkPass(IUnknown* device, uint64_t custom_hash)
{
   if (DeviceState* state = FindMutable(device))
   {
      std::scoped_lock lock(state->mutex);
      if (std::ranges::find(state->custom_markers, custom_hash) == state->custom_markers.end())
         state->custom_markers.push_back(custom_hash);
   }
}

bool HasPass(IUnknown* device, uint64_t custom_hash)
{
   if (const DeviceState* state = Find(device))
   {
      std::scoped_lock lock(state->mutex);
      return std::ranges::find(state->custom_markers, custom_hash) != state->custom_markers.end();
   }
   return false;
}

void AdvanceFrame(IUnknown* device)
{
   if (DeviceState* state = FindMutable(device))
   {
      const int64_t now = GetQpcNow();
      std::scoped_lock lock(state->mutex);
      state->timing.frame_index++;
      if (state->last_present_qpc != 0 && now > state->last_present_qpc)
      {
         const float delta = float(double(now - state->last_present_qpc) / double(GetQpcFrequency()));
         state->timing.time_delta = std::clamp(delta, 1e-4f, 1.f);
      }
      state->last_present_qpc = now;
      state->markers.fill(false);
      state->custom_markers.clear();
      state->camera.camera_cut = false;
   }
}
} // namespace FrameState
} // namespace Luma
