#include "Frame/Frame.hpp"

#include <atomic>
#include <mutex>

#include <Windows.h>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/Lifetime.hpp"

namespace Luma {
namespace Frame {
// Read at every LumaData write and LumaSettings upload and by the game, written at swapchain changes: a lock keeps the three
// consistent. Never held with another lock.
struct DeviceState
{
   mutable std::mutex mutex;
   Resolutions resolutions;

   explicit DeviceState(Math::uint2 size) : resolutions{size, size, size, size, size} {}
};

namespace {
std::atomic<uint64_t> generation = 0;

// 1.0 starts from the screen's size, "an arbitrary guess that is better than 1920x1080", until the swapchain is created
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
} // namespace Frame
} // namespace Luma
