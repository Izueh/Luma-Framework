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

// ------------------------------------------------------------------------------------------------
// Dynamic Resolution Handling
// ------------------------------------------------------------------------------------------------
// FF7R uses dynamic resolution where:
// - cb1_data[126].xy = Full texture size (e.g., 1920x1080)
// - cb1_data[126].zw = Full texture pixel size (1/1920, 1/1080)  
// - cb1_data[122].xy = Actual render resolution (e.g., 1280x720 at 67% scale)
//
// For GTAO with dynamic resolution:
// - ViewportPixelSize uses full texture pixel size (for UV calculations)
// - NDCToViewMul_x_PixelSize uses actual render resolution pixel size (for screen-space radius)
// - SampleUVClamp limits sampling to the valid rendered region
// ------------------------------------------------------------------------------------------------

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
    // Full texture size and pixel size (for source texture UV calculations)
    constants.ViewportPixelSize = cb1_data[126].zw;
    constants.ViewportSize = cb1_data[126].xy;
    
    // Viewport offset - where the rendered region starts in the texture
    constants.ViewportOffset = cb1_data[121].xy;

    // Actual render resolution
    float2 actualRenderSize = cb1_data[122].xy;
    float2 actualRenderPixelSize = cb1_data[122].zw;
    
    // Render pixel size for working texture UV calculations
    constants.RenderPixelSize = actualRenderPixelSize;

    // FOV calculation
    float fov = LumaData.GameData.GTAO.FOV;
    if (fov <= 0.001f) fov = 1.0472f; // Default to 60 degrees in radians

    float tanHalfFovY = tan(fov * 0.5);
    float aspectRatio = actualRenderSize.x / actualRenderSize.y;
    float tanHalfFovX = tanHalfFovY * aspectRatio;

    // NDC to view conversion
    // Maps pixel coordinate in [0, actualRenderSize] to viewspace
    constants.NDCToViewMul = float2(tanHalfFovX * 2.0, tanHalfFovY * -2.0) * actualRenderPixelSize;
    constants.NDCToViewAdd = float2(-tanHalfFovX, tanHalfFovY);

    // NDCToViewMul_x_PixelSize represents viewspace size of one actual render pixel
    constants.NDCToViewMul_x_PixelSize = float2(tanHalfFovX * 2.0, tanHalfFovY * -2.0) * actualRenderPixelSize;

    constants.EffectRadius = cb0_data[18].w;
    constants.EffectFalloffRange = 0.5;
    constants.RadiusMultiplier = 500.0;
    constants.DenoiseBlurBeta = 1.2;
    constants.SampleDistributionPower = 2.0;
    constants.ThinOccluderCompensation = cb0_data[18].z;
    constants.FinalValuePower = 1.0;
    constants.DepthMIPSamplingOffset = 3.3;

    // UV clamp for source textures (with viewport offset)
    constants.SampleUVClamp = (constants.ViewportOffset + actualRenderSize) * constants.ViewportPixelSize;
    
    // UV clamp for working textures (no viewport offset, uses RenderPixelSize)
    constants.WorkingUVClamp = float2(1.0, 1.0); // Working textures are sized to actualRenderSize
    
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
    
    // Sample normal from game's normal buffer (needs viewport offset for source textures)
    float2 normalSampleUV = (constants.ViewportOffset + dtid + 0.5) * constants.ViewportPixelSize;
    normalSampleUV = min(normalSampleUV, constants.SampleUVClamp);
    
    // Load world-space normal (stored as 0-1, convert to -1 to +1)
    float3 worldNormal = tex1.SampleLevel(smp, normalSampleUV, 0).xyz;
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