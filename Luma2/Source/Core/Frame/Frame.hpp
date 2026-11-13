#pragma once

#include <cstdint>

#include "Common/Math.hpp"

struct IUnknown;

// The per-frame model. Here, each device's resolutions (1.0's DeviceData fields): Core sets the output at swapchain creation and
// resize, a game's SR or TAA path the render one (Game::Frame). Read from any thread.
namespace Luma {
namespace Frame {
struct Resolutions
{
   Math::uint2 output = {1, 1}; // The swapchain's (1.0's output_resolution)
   Math::uint2 render = {1, 1}; // What the game renders the scene at, the output until something sets it
   Math::uint2 previous_render = {1, 1}; // The render resolution before the last change
};

// A device's resolutions
struct DeviceState;

// With each device, before its callbacks: the screen's size until a swapchain says otherwise (1.0's guess)
void CreateDeviceState(IUnknown* device);
// Any thread, the three as they were set together. A device Luma doesn't know (or a game's proxy of one it does) gives the defaults.
Resolutions Get(IUnknown* device);
// The same without the device's private data lookup, for a hot path that finds the state once (a context, at every draw): null for
// a device Luma doesn't know, valid while the device is. Read gives the defaults for null.
const DeviceState* Find(IUnknown* device);
Resolutions Read(const DeviceState* state);
// The swapchain's size: render and previous render follow it (until SR changes them, Step 7)
void SetOutputResolution(IUnknown* device, Math::uint2 size);
void SetRenderResolution(IUnknown* device, Math::uint2 size);
// Any thread: changes after every change of any device's resolutions (part of LumaSettings' stamp)
uint64_t GetGeneration();
} // namespace Frame
} // namespace Luma
