// XeGTAO implementation for Final Fantasy VII Remake
// For the reference see: https://github.com/GameTechDev/XeGTAO

// Game Constant Buffers - needed for depth linearization and normal transformation
cbuffer cb1 : register(b1) { float4 cb1_data[140]; }
cbuffer cb0 : register(b0) { float4 cb0_data[21]; }

// Luma Constant Buffers
#include "Includes/Common.hlsl"

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
// #define VIEWPORT_PIXEL_SIZE (cb1_data[126].zw) <-- Removed

// ------------------------------------------------------------------------------------------------
// Dynamic Resolution Handling
// ------------------------------------------------------------------------------------------------

// Calculate the scale ratio for internal use (Projection Matrix correction)
#define DYNAMIC_RES_SCALE float2(cb1_data[122].x / cb1_data[126].x, cb1_data[122].y / cb1_data[126].y)

// We set the Scale to 1.0 because VIEWPORT_PIXEL_SIZE is already based on the full texture size.
// This ensures sampling UVs are correct (0 to Scale).
#define XE_GTAO_RENDER_RESOLUTION_SCALE float2(1.0, 1.0)

// We still clamp to the valid dynamic region
#define XE_GTAO_SAMPLE_UV_CLAMP float2(cb1_data[122].x * VIEWPORT_PIXEL_SIZE.x, cb1_data[122].y * VIEWPORT_PIXEL_SIZE.y)

// ------------------------------------------------------------------------------------------------
// Camera Parameters
// ------------------------------------------------------------------------------------------------

static float g_TanHalfFovY;
static float g_TanHalfFovX;


// Correct the View Space reconstruction.
// Since our UVs only go from 0 to Scale, we need to stretch the Multiplier so that 'Scale' maps to the edge of the screen.
#define NDC_TO_VIEW_MUL float2((g_TanHalfFovX * 2.0) / DYNAMIC_RES_SCALE.x, (g_TanHalfFovY * -2.0) / DYNAMIC_RES_SCALE.y)
#define NDC_TO_VIEW_ADD float2(-g_TanHalfFovX, g_TanHalfFovY)

// Effect radius: cb0[18].w * 500 is world-space radius
#define EFFECT_RADIUS (cb0_data[18].w)
#define RADIUS_MULTIPLIER 500.0

// Thin occluder compensation from game: cb0[18].z
#define THIN_OCCLUDER_COMPENSATION (cb0_data[18].z)

#define EFFECT_FALLOFF_RANGE 0.5

// Final visibility power adjustment
#define FINAL_VALUE_POWER 1.0

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

// float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
// {
//     float depthLinearizeMul = CAMERA_CLIP_FAR * CAMERA_CLIP_NEAR / (CAMERA_CLIP_FAR - CAMERA_CLIP_NEAR);
//     float depthLinearizeAdd = CAMERA_CLIP_FAR / (CAMERA_CLIP_FAR - CAMERA_CLIP_NEAR);

//     // correct the handedness issue. need to make sure this below is correct, but I think it is.
//     if (depthLinearizeMul * depthLinearizeAdd < 0.0) {
//         depthLinearizeAdd = -depthLinearizeAdd;
//     }

//     // Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"
//     return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
// }

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

void ComputeParams(inout GTAOConstants constants)
{
    constants.ViewportPixelSize = cb1_data[126].zw;
    constants.ViewportSize = cb1_data[126].xy;

    float2 renderResolutionScale = float2(cb1_data[122].x / cb1_data[126].x, cb1_data[122].y / cb1_data[126].y);

    // FOV calculation
    float fov = LumaData.GameData.GTAO.FOV;
    if (fov <= 0.001f) fov = 1.0472f; // Default to 60 degrees in radians

    float tanHalfFovY = tan(fov * 0.5);
    float aspectRatio = cb1_data[122].x / cb1_data[122].y;
    float tanHalfFovX = tanHalfFovY * aspectRatio;

    constants.NDCToViewMul = float2((tanHalfFovX * 2.0) / renderResolutionScale.x, (tanHalfFovY * -2.0) / renderResolutionScale.y);
    constants.NDCToViewAdd = float2(-tanHalfFovX, tanHalfFovY);

    constants.NDCToViewMul_x_PixelSize = constants.NDCToViewMul * constants.ViewportPixelSize;

    constants.EffectRadius = cb0_data[18].w;
    constants.EffectFalloffRange = 0.5;
    constants.RadiusMultiplier = 500.0;
    constants.DenoiseBlurBeta = 1.2;
    constants.SampleDistributionPower = 2.0;
    constants.ThinOccluderCompensation = cb0_data[18].z;
    constants.FinalValuePower = 1.0;
    constants.DepthMIPSamplingOffset = 3.3;

    constants.SampleUVClamp = float2(cb1_data[122].x * constants.ViewportPixelSize.x, cb1_data[122].y * constants.ViewportPixelSize.y);
    constants.OcclusionTermScale = 1.5;
}

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
void prefilter_depths16x16_cs(uint2 dtid: SV_DispatchThreadID, uint2 gtid: SV_GroupThreadID)
{
    GTAOConstants constants;
    ComputeParams(constants);
    XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, smp, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4, constants);
}

// ------------------------------------------------------------------------------------------------
// Pass 2: Main GTAO Pass
// ------------------------------------------------------------------------------------------------

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_cs(uint2 dtid: SV_DispatchThreadID)
{
    GTAOConstants constants;
    ComputeParams(constants);
    const float2 normalizedScreenPos = (dtid + 0.5) * constants.ViewportPixelSize;

    // Load world-space normal (stored as 0-1, convert to -1 to +1)
    // LUMA: Removed manual scaling here as normalizedScreenPos is already correct texture UV
    float2 samplePos = min(normalizedScreenPos, constants.SampleUVClamp);
    float3 worldNormal = tex1.SampleLevel(smp, samplePos, 0).xyz;
    worldNormal = worldNormal * 2.0 - 1.0;
    worldNormal = normalize(worldNormal);

    // Transform to view space
    float3 viewspaceNormal = mul(worldNormal, GetWorldToViewMatrix());

    viewspaceNormal = normalize(viewspaceNormal);

    uint temporalIndex = LumaSettings.FrameIndex;

    XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid, temporalIndex), viewspaceNormal, tex0, smp, ao_term_and_edges, constants);
}

// ------------------------------------------------------------------------------------------------
// Pass 3: Denoise Pass
// ------------------------------------------------------------------------------------------------

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_pass_cs(uint2 dtid: SV_DispatchThreadID)
{
    GTAOConstants constants;
    ComputeParams(constants);
    // Normal denoise: each thread handles 2 horizontal pixels
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    XeGTAO_Denoise(pix_coord_base, tex0, smp, final_output, true, constants);
}