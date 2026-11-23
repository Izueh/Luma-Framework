#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <string>

#include <d3d11.h>
#include <d3d12.h>

// Live texture debug draw: snapshots a selected game texture on the GPU and binds it to Luma_DisplayComposition.hlsl (t2..t8)
// with CustomData2 (display flags + texture dimension bits) and CustomData3 (mip level). Development builds only.
namespace Luma {
namespace Trace {
namespace DebugDraw {
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
void SetTarget(std::string resource_id);
std::string GetTarget();
void Clear();

Options GetOptions();
void SetOptions(const Options& options);

// Packs Options + texture dimension bits into LumaData.CustomData2 for Luma_DisplayComposition.hlsl
uint32_t PackFlags(const Options& options, uint32_t dimension_bits);

// D3D11: snapshots `resource` if its address matches the target (or its mirror)
void CaptureD3D11(ID3D11DeviceContext* context, ID3D11Resource* resource);
// D3D11: scans bound render targets, depth, SRVs and UAVs on `context` and snapshots if any matches
void CaptureBoundD3D11(ID3D11DeviceContext* context);
// D3D11: binds the snapshot SRV into t2..t8 and sets CustomData2/CustomData3
bool BindD3D11(ID3D11DeviceContext* context, uint32_t& custom_data_2, float& custom_data_3);
void ReleaseD3D11(const ID3D11Device* device);

// D3D12: snapshots `resource` (currently in `current_state`) if its address matches the target
void CaptureD3D12(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES current_state);
// D3D12: writes the snapshot SRV into the composition descriptor table (t2..t8) and sets CustomData2/CustomData3
bool BindD3D12(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE table_start, uint32_t view_increment, uint32_t& custom_data_2, float& custom_data_3);
void ReleaseD3D12(const ID3D12Device* device);
} // namespace DebugDraw
} // namespace Trace
} // namespace Luma
#endif
