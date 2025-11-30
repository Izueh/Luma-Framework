// XeGTAO implementation for Final Fantasy VII Remake
//
// Based on the Bioshock Infinite implementation and UE4 GTAO optimizations
// For the reference see: https://github.com/GameTechDev/XeGTAO

// Game Constant Buffers - needed for depth linearization and normal transformation
cbuffer cb1 : register(b1) { float4 cb1_data[140]; }
cbuffer cb0 : register(b0) { float4 cb0_data[21]; }

// Luma Constant Buffers
#include "Includes/Common.hlsl"

// ------------------------------------------------------------------------------------------------
// XeGTAO Configuration based on quality setting from Settings.hlsl
// ------------------------------------------------------------------------------------------------

// XeGTAO SLICE_COUNT vs UE4 NumAngles:
// - XeGTAO samples bilaterally (+/- omega), so effective directions = SLICE_COUNT * 2
// - Higher SLICE_COUNT = better angular coverage, fewer artifacts in corners

#if XE_GTAO_QUALITY == 0 // Low - 6 effective directions, 2 radial samples
    #define SLICE_COUNT 3.0
    #define STEPS_PER_SLICE 2.0
#elif XE_GTAO_QUALITY == 1 // Medium - 10 effective directions, 2 radial samples
    #define SLICE_COUNT 5.0
    #define STEPS_PER_SLICE 2.0
#elif XE_GTAO_QUALITY == 2 // High - 14 effective directions, 3 radial samples
    #define SLICE_COUNT 7.0
    #define STEPS_PER_SLICE 3.0
#elif XE_GTAO_QUALITY == 3 // Very High - 18 effective directions, 3 radial samples
    #define SLICE_COUNT 9.0
    #define STEPS_PER_SLICE 3.0
#elif XE_GTAO_QUALITY == 4 // Ultra - 24 effective directions, 4 radial samples
    #define SLICE_COUNT 12.0
    #define STEPS_PER_SLICE 4.0
#endif

// Get resolution from LumaData
#define VIEWPORT_PIXEL_SIZE (cb1_data[122].zw)

// ------------------------------------------------------------------------------------------------
// Camera Parameters
// ------------------------------------------------------------------------------------------------

static float g_TanHalfFovY;
static float g_TanHalfFovX;

void ComputeCameraParams()
{
    // FOV is already in radians
    float fov = LumaData.GameData.GTAO.FOV;
    if (fov <= 0.001f) fov = 1.0472f; // Default to 60 degrees in radians
    
    g_TanHalfFovY = tan(fov * 0.5);
    float aspectRatio = cb1_data[122].x / cb1_data[122].y;
    g_TanHalfFovX = g_TanHalfFovY * aspectRatio;
}

#define NDC_TO_VIEW_MUL float2(g_TanHalfFovX * 2.0, g_TanHalfFovY * -2.0)
#define NDC_TO_VIEW_ADD float2(-g_TanHalfFovX, g_TanHalfFovY)

// ------------------------------------------------------------------------------------------------
// Effect Parameters - Read from game's cb0
// ------------------------------------------------------------------------------------------------

// Effect radius: cb0[18].w * 500 is world-space radius
#define EFFECT_RADIUS (cb0_data[18].w * 500.0)
#define RADIUS_MULTIPLIER 1.0

// Thin occluder compensation from game: cb0[18].z
#define THIN_OCCLUDER_COMPENSATION (2.0f)

// Minimum radius scale: cb0[3].z / cb0[20].x * sqrt(2)
#define MIN_RADIUS_SCALE (cb0_data[3].z * rcp(cb0_data[20].x) * 1.41421354)

// Game uses hardcoded falloff values:
// - Falloff start: 500 units, range: 100 units (1/0.01)
// - This means falloff starts at 500/effectRadius of the radius
// For XeGTAO, we express as fraction: falloff starts at (1 - EFFECT_FALLOFF_RANGE)
// Game: starts at 500, ends at 600 for a 600-unit radius = starts at 83%
// We'll use 0.5 as a reasonable default since game's is fixed
#define EFFECT_FALLOFF_RANGE (saturate(100.0f / max(EFFECT_RADIUS, 1.0f)))

// Maximum screen-space radius in pixels (prevents artifacts on close surfaces)
#define XE_GTAO_MAX_PIXEL_RADIUS 256.0

// Sample distribution power (2.0 = quadratic, concentrates samples near center)
#define SAMPLE_DISTRIBUTION_POWER 3.0

// Final visibility power adjustment
#define FINAL_VALUE_POWER 1.0

// Denoise blur strength
#define DENOISE_BLUR_BETA 1.2

// Depth MIP sampling offset
#define DEPTH_MIP_SAMPLING_OFFSET 3.3

// ------------------------------------------------------------------------------------------------
// Depth Handling
// ------------------------------------------------------------------------------------------------

// Linearize depth using the game's projection matrix constants (from cb1[57])
// Result: positive values, higher = farther (non-inverted after linearization)
float XeGTAO_ScreenSpaceToViewSpaceDepth(float screenDepth)
{
    float z1 = screenDepth * cb1_data[57].x + cb1_data[57].y;
    float z2 = screenDepth * cb1_data[57].z - cb1_data[57].w;
    z2 = rcp(z2);
    return z1 + z2;
}

// ------------------------------------------------------------------------------------------------
// World-to-View Matrix for Normal Transformation
// ------------------------------------------------------------------------------------------------

// Extract the 3x3 world-to-view rotation matrix from cb1[8-10]
float3x3 GetWorldToViewMatrix()
{
    return float3x3(
        cb1_data[8].xyz,
        cb1_data[9].xyz,
        cb1_data[10].xyz
    );
}

// ------------------------------------------------------------------------------------------------
// Include XeGTAO after defining all the required macros
// ------------------------------------------------------------------------------------------------

#include "Includes/XeGTAO.hlsli"

// ------------------------------------------------------------------------------------------------
// Resources
// ------------------------------------------------------------------------------------------------

SamplerState smp : register(s0);

// Input textures
Texture2D tex0 : register(t0);  // Depth buffer or working depth
Texture2D tex1 : register(t1);  // Normals (world space, 0-1 encoded)

// UAVs - same layout as Bioshock
RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
RWTexture2D<unorm float2> ao_term_and_edges : register(u0);
RWTexture2D<unorm float4> final_output : register(u0);

// ------------------------------------------------------------------------------------------------
// Hilbert Curve for Spatio-Temporal Noise
// ------------------------------------------------------------------------------------------------

#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

#define XE_HILBERT_LEVEL 6U
#define XE_HILBERT_WIDTH (1U << XE_HILBERT_LEVEL)
#define XE_HILBERT_AREA (XE_HILBERT_WIDTH * XE_HILBERT_WIDTH)

uint HilbertIndex(uint posX, uint posY)
{
    uint index = 0U;
    [unroll]
    for (uint curLevel = XE_HILBERT_WIDTH / 2U; curLevel > 0U; curLevel /= 2U)
    {
        uint regionX = (posX & curLevel) > 0U;
        uint regionY = (posY & curLevel) > 0U;
        index += curLevel * curLevel * ((3U * regionX) ^ regionY);
        if (regionY == 0U)
        {
            if (regionX == 1U)
            {
                posX = XE_HILBERT_WIDTH - 1U - posX;
                posY = XE_HILBERT_WIDTH - 1U - posY;
            }
            uint temp = posX;
            posX = posY;
            posY = temp;
        }
    }
    return index;
}

// Use frame index from LumaData for temporal variation
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64);
    return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

// ------------------------------------------------------------------------------------------------
// Pass 1: Prefilter Depths (16x16 blocks)
// ------------------------------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
    ComputeCameraParams();
    XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, smp, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
}

// ------------------------------------------------------------------------------------------------
// Pass 2: Main GTAO Pass
// ------------------------------------------------------------------------------------------------

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    ComputeCameraParams();
    
    const float2 normalizedScreenPos = (dtid + 0.5) * VIEWPORT_PIXEL_SIZE;
    
    // Load world-space normal (stored as 0-1, convert to -1 to +1)
    float3 worldNormal = tex1.SampleLevel(smp, normalizedScreenPos, 0).xyz;
    worldNormal = worldNormal * 2.0 - 1.0;
    worldNormal = normalize(worldNormal);
    
    // Transform to view space
    float3 viewspaceNormal = mul(worldNormal, GetWorldToViewMatrix());
    
    viewspaceNormal = normalize(viewspaceNormal);

    uint temporalIndex = LumaSettings.FrameIndex;
    
    XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid, temporalIndex), viewspaceNormal, tex0, smp, ao_term_and_edges);
}

// ------------------------------------------------------------------------------------------------
// Pass 3: Denoise Pass
// ------------------------------------------------------------------------------------------------

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    // Normal denoise: each thread handles 2 horizontal pixels
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    XeGTAO_Denoise(pix_coord_base, tex0, smp, final_output, true);
}