#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Common/Device.hpp"
#include "Common/Math.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Resources/ResourceUpgrades.hpp"

namespace Luma {
namespace Resources {
namespace Scaling {
enum class Mode : uint8_t
{
   None,
   // Creates scaled mirrors (output/custom size) and proportionally rescales viewports, scissors, copy boxes and clear rects
   ResourcesAndViewports,
   // Render targets are already output-sized (UE DRS sub-rect); expands viewports and scissors to target_resolution while active
   ViewportsOnly,
};

struct Options
{
   Mode mode = Mode::None;
   // Optional declarative activation window by shader hash; games may also call Begin/End from draw callbacks
   std::vector<uint32_t> activate_after_hashes;
   std::vector<uint32_t> deactivate_at_hashes;
   bool scale_viewports = true;
   bool scale_scissors = true;
   // Reserved for games needing stencil stand-ins or per-frame pooled targets (LUMA_UNIMPLEMENTED until ported)
   bool stencil_stand_in = false;
   bool pool_by_frame = false;
};

struct Viewport
{
   float x = 0.f;
   float y = 0.f;
   float width = 0.f;
   float height = 0.f;
   float min_depth = 0.f;
   float max_depth = 1.f;
};

struct Rect
{
   int32_t left = 0;
   int32_t top = 0;
   int32_t right = 0;
   int32_t bottom = 0;
};

struct Box
{
   uint32_t left = 0;
   uint32_t top = 0;
   uint32_t front = 0;
   uint32_t right = 0;
   uint32_t bottom = 0;
   uint32_t back = 1;
};

struct Stats
{
   Mode mode = Mode::None;
   bool active = false;
   uint32_t activating_thread = 0;
   uint32_t scaled_mirrors = 0;
   uint32_t pool_size = 0;
   uint32_t invalidations = 0;
   uint32_t viewport_rescales = 0;
   uint32_t scissor_rescales = 0;
   uint32_t copy_box_rescales = 0;
   uint32_t clear_rect_rescales = 0;
   uint32_t scaled_blits = 0;
   uint32_t cross_thread_overlaps = 0;
};

void Register();
void SetOptions(const Options& options);
const Options& GetOptions();
bool IsNeeded();
bool IsEnabled();
std::string_view GetModeName(Mode mode);
Hooks::D3D11::Functions GetD3D11Functions();
Hooks::D3D12::Functions GetD3D12Functions();

// Per-device activation window (thread-affine: scoped to the thread that called Begin)
void Begin(Device device);
void End(Device device);
void ResetFrame(Device device);
void DestroyDeviceState(Device device);
bool IsActive(Device device);
bool IsActiveOnCurrentThread(Device device);
// Returns true and counts an overlap if another thread currently holds the active scaling window
bool CheckCrossThreadOverlap(Device device);
void SetTargetResolution(Device device, Math::uint2 size);
Math::uint2 GetTargetResolution(Device device);
bool ShouldActivateAfter(uint32_t shader_hash);
bool ShouldDeactivateAt(uint32_t shader_hash);

// Returns true if render or output resolution changed since the last check (triggers scaled mirror invalidation)
bool ConsumeResolutionChange(Device device, Math::uint2 render, Math::uint2 output);

struct ScaleContext
{
   bool active = false;
   bool from_output_sized_source = false;
   uint32_t aspect_ratio_pixel_threshold = 1;
};

// Pure scaling math helpers
bool ShouldScaleTexture(const Upgrade::TextureInfo& desc, const Upgrade::FrameState& state, ScaleContext context);
Math::uint2 ComputeScaledSize(Math::uint2 original, Math::uint2 render, Math::uint2 output);
Viewport RescaleViewport(const Viewport& viewport, Math::float2 scale);
Rect RescaleRect(const Rect& rect, Math::float2 scale);
Box RescaleBox(const Box& box, Math::float2 scale);

void CountScaledMirror(int32_t delta);
void CountPoolSize(int32_t delta);
void CountInvalidation();
void CountViewportRescale();
void CountScissorRescale();
void CountCopyBoxRescale();
void CountClearRectRescale();
void CountScaledBlit();
Stats GetStats(Device device = {});
} // namespace Scaling
} // namespace Resources
} // namespace Luma
