#pragma once

#include <cstdint>

#include "Common/Device.hpp"
#include "Common/Math.hpp"

// The per-frame model: each device's resolutions, camera data, timing, and pass markers. Read from any thread.
namespace Luma {
namespace FrameState {
// Kept distinct: output and swapchain are equal unless Luma presents at another size
struct Resolutions
{
   Math::uint2 output = {1, 1}; // The back buffer the game asked for, what upscaling targets
   Math::uint2 render = {1, 1}; // What the game renders the scene at, the output until something sets it
   Math::uint2 previous_render = {1, 1}; // The render resolution before the last change
   Math::uint2 swapchain = {1, 1}; // The real buffers Luma presents
   Math::uint2 display = {1, 1}; // The mode of the monitor the window is on
};

struct Camera
{
   Math::float2 jitter_pixels = {0.f, 0.f}; // Subpixel jitter in [-0.5, 0.5] pixel space
   float vert_fov = 0.f; // Radians, 0 if unknown
   float near_plane = 0.01f; // Meters
   float far_plane = 1000.f; // Meters
   float pre_exposure = 0.f; // Ignored when 0
   bool camera_cut = false;
};

struct Timing
{
   uint64_t frame_index = 0;
   float time_delta = 1.f / 60.f; // Seconds
};

enum class PassMarker : uint8_t
{
   Taa = 0,
   Sr,
   Upscaling,
   MainPostProcessing,
   Count,
};

// A device's per-frame state
struct DeviceState;

// With each device, before its callbacks: the screen's size until a swapchain says otherwise
void CreateDeviceState(Device device);
void DestroyDeviceState(Device device);
// Any thread, the three as they were set together. A device Luma doesn't know (or a game's proxy of one it does) gives the defaults.
Resolutions Get(Device device);
// The same without the device table lookup, for a hot path that finds the state once (a context, at every draw): null for
// a device Luma doesn't know, valid while the device is. Read gives the defaults for null.
const DeviceState* Find(Device device);
Resolutions Read(const DeviceState* state);
// The game's back buffer size: render and previous render follow it (until SR changes them), and the swapchain's until
// SetSwapchainResolution says otherwise
void SetOutputResolution(Device device, Math::uint2 size);
// The real buffers' and the monitor's, after the output's
void SetSwapchainResolution(Device device, Math::uint2 swapchain, Math::uint2 display);
void SetRenderResolution(Device device, Math::uint2 size);
// Any thread: changes after every change of any device's resolutions (part of LumaSettings' stamp)
uint64_t GetGeneration();

void SetCamera(Device device, const Camera& camera);
Camera GetCamera(Device device);
Timing GetTiming(Device device);

void MarkPass(Device device, PassMarker marker);
bool HasPass(Device device, PassMarker marker);
void MarkPass(Device device, uint64_t custom_hash);
bool HasPass(Device device, uint64_t custom_hash);

// Presenting thread: advances frame_index/time_delta and resets per-frame pass markers and camera_cut
void AdvanceFrame(Device device);
} // namespace FrameState
} // namespace Luma
