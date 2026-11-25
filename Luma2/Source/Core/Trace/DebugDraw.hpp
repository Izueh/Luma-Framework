#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3d12.h>

#include "Common/ShaderStage.hpp"
#include "Game/GameEvents.hpp"
#include "Shaders/ShaderStages.hpp"

// Live texture debug draw: snapshots a selected game texture on the GPU and binds it to Luma_DisplayComposition.hlsl (t2..t8)
// with CustomData2 (display flags + texture dimension bits) and CustomData3 (mip level). Development builds only.
namespace Luma {
namespace Trace {
namespace DebugDraw {
enum class ViewMode : uint32_t
{
   Resource, // Direct resource address (ResourcesPanel or non-shader events)
   RenderTarget,
   ShaderResource,
   UnorderedAccess,
   Depth,
};

struct PassTarget
{
   uint32_t hash = 0; // 0 for Luma passes
   ShaderStage stage = ShaderStage::Pixel;
   std::string luma_pass; // Non-empty for Luma passes (event.name)
   uint32_t instance = 0; // 0-based call index of this shader/pass in the frame
   ViewMode mode = ViewMode::RenderTarget;
   uint32_t slot = 0;
   uint32_t mip = 0;

   bool operator==(const PassTarget&) const = default;

   bool SamePass(const PassTarget& other) const
   {
      return hash == other.hash && stage == other.stage && luma_pass == other.luma_pass && instance == other.instance;
   }
};

struct Options
{
   bool fullscreen = false;
   bool render_resolution_scale = false;
   bool show_alpha = false;
   bool premultiply_alpha = false;
   bool invert_colors = false;
   bool linear_to_gamma = false;
   bool gamma_to_linear = false;
   bool flip_y = false;
   bool do_abs = false;
   bool do_saturate = false;
   bool red_only = false;
   bool background_passthrough = false;
   bool zoom_4x = false;
   bool bilinear = false;
   bool tonemap = false;
   bool srgb = false;
   bool uv_to_pixel_space = false;
   bool denormalize = false;
   int32_t mip_level = 0;
};

// Relaxed atomic check for hot draw paths
bool IsActive();
// Direct resource-address targeting (ResourcesPanel, Copy/Clear/CPU events)
void SetTarget(std::string resource_id);
std::string GetTarget();
// Pass + instance + view targeting (matches Luma 1.0 Trace behavior)
void SetPassTarget(PassTarget target);
std::optional<PassTarget> GetPassTarget();
bool IsPassTarget(const PassTarget& target);
bool IsSamePass(const PassTarget& target);
// Last view mode/slot used by SetPassTarget (for magnifier quick-browse across passes)
ViewMode GetLastViewMode();
uint32_t GetLastViewSlot();
std::string DescribeTarget();
void Clear();
// Resets per-frame instance counter on Present
void OnPresent();

Options GetOptions();
void SetOptions(const Options& options);

// Packs Options + texture dimension bits into LumaData.CustomData2 for Luma_DisplayComposition.hlsl
uint32_t PackFlags(const Options& options, uint32_t dimension_bits);

// D3D11: snapshots `resource` if its address matches the target (or its mirror)
void CaptureD3D11(ID3D11DeviceContext* context, ID3D11Resource* resource);
// D3D11: snapshots the target view on a game draw/dispatch (pass-instance mode or resource-address fallback)
bool CaptureCallD3D11(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled);
// D3D11: snapshots the target view on a Luma pass
void CaptureLumaPassD3D11(ID3D11DeviceContext* context, std::string_view pass_name);
// D3D11: snapshots an SR pass input or output texture
void CaptureSrPassD3D11(ID3D11DeviceContext* context, std::string_view pass_name, ID3D11Resource* color, ID3D11Resource* depth,
   ID3D11Resource* motion_vectors, ID3D11Resource* exposure, ID3D11Resource* reactive, ID3D11Resource* transparency, ID3D11Resource* output);
// D3D11: scans bound render targets, depth, SRVs and UAVs on `context` and snapshots if any matches
void CaptureBoundD3D11(ID3D11DeviceContext* context);
// D3D11: binds the snapshot SRV into t2..t8 and sets CustomData2/CustomData3
bool BindD3D11(ID3D11DeviceContext* context, uint32_t& custom_data_2, float& custom_data_3);
void ReleaseD3D11(const ID3D11Device* device);

// D3D12: snapshots `resource` (currently in `current_state`) if its address matches the target
void CaptureD3D12(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES current_state);
// D3D12: snapshots the target view on a game draw/dispatch (pass-instance mode)
bool CaptureCallD3D12(const Game::D3D12::Call& call, Shaders::StageSet stages, bool handled);
// D3D12: snapshots an SR pass input or output texture
void CaptureSrPassD3D12(ID3D12GraphicsCommandList* list, std::string_view pass_name, ID3D12Resource* color, ID3D12Resource* depth,
   ID3D12Resource* motion_vectors, ID3D12Resource* exposure, ID3D12Resource* reactive, ID3D12Resource* transparency, ID3D12Resource* output);
// D3D12: writes the snapshot SRV into the composition descriptor table (t2..t8) and sets CustomData2/CustomData3
bool BindD3D12(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE table_start, uint32_t view_increment, uint32_t& custom_data_2, float& custom_data_3);
void ReleaseD3D12(const ID3D12Device* device);
} // namespace DebugDraw
} // namespace Trace
} // namespace Luma
#endif
