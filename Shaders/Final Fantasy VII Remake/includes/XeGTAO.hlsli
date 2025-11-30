// XeGTAO adopted for Final Fantasy VII Remake.
// With UE4-style optimizations (squared distance attenuation, screen-space radius clamping)
//
// Source: https://github.com/GameTechDev/XeGTAO

#ifndef __XE_GTAO_HLSLI__
#define __XE_GTAO_HLSLI__

// ------------------------------------------------------------------------------------------------
// Required defines (must be set before including this header)
// ------------------------------------------------------------------------------------------------

#ifndef VIEWPORT_PIXEL_SIZE
#error "VIEWPORT_PIXEL_SIZE must be defined"
#endif

#ifndef NDC_TO_VIEW_MUL
#error "NDC_TO_VIEW_MUL must be defined"
#endif

#ifndef NDC_TO_VIEW_ADD
#error "NDC_TO_VIEW_ADD must be defined"
#endif

// ------------------------------------------------------------------------------------------------
// User configurable defaults
// ------------------------------------------------------------------------------------------------

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 200.0
#endif

#ifndef RADIUS_MULTIPLIER
#define RADIUS_MULTIPLIER 1.0
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.5
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 2.0
#endif

#ifndef THIN_OCCLUDER_COMPENSATION
#define THIN_OCCLUDER_COMPENSATION 0.5
#endif

#ifndef FINAL_VALUE_POWER
#define FINAL_VALUE_POWER 2.2
#endif

#ifndef DEPTH_MIP_SAMPLING_OFFSET
#define DEPTH_MIP_SAMPLING_OFFSET 3.3
#endif

#ifndef SLICE_COUNT
#define SLICE_COUNT 7.0
#endif

#ifndef STEPS_PER_SLICE
#define STEPS_PER_SLICE 3.0
#endif

#ifndef DENOISE_BLUR_BETA
#define DENOISE_BLUR_BETA 1.2
#endif

#ifndef XE_GTAO_MAX_PIXEL_RADIUS
#define XE_GTAO_MAX_PIXEL_RADIUS 256.0
#endif

// ------------------------------------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------------------------------------

#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * VIEWPORT_PIXEL_SIZE)
#define XE_GTAO_DEPTH_MIP_LEVELS 4.0
#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5

#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

// ------------------------------------------------------------------------------------------------
// Depth linearization (must be implemented by the including file)
// ------------------------------------------------------------------------------------------------

// This must be defined by the implementation file before including this header!
// float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth);

// ------------------------------------------------------------------------------------------------
// Helper Functions
// ------------------------------------------------------------------------------------------------

float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 0.0, 1000000.0);
}

float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    const float depthRangeScaleFactor = 0.75;
    const float effectRadius = depthRangeScaleFactor * EFFECT_RADIUS * RADIUS_MULTIPLIER;
    const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
    const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float weight0 = saturate((maxDepth - depth0) * falloffMul + falloffAdd);
    float weight1 = saturate((maxDepth - depth1) * falloffMul + falloffAdd);
    float weight2 = saturate((maxDepth - depth2) * falloffMul + falloffAdd);
    float weight3 = saturate((maxDepth - depth3) * falloffMul + falloffAdd);

    float weightSum = weight0 + weight1 + weight2 + weight3;
    return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) * rcp(weightSum);
}

// ------------------------------------------------------------------------------------------------
// Prefilter Depths (generates depth MIP chain)
// ------------------------------------------------------------------------------------------------

groupshared float g_scratchDepths[8][8];

void XeGTAO_PrefilterDepths16x16(uint2 dispatchThreadID, uint2 groupThreadID, Texture2D sourceNDCDepth, SamplerState depthSampler, RWTexture2D<float> outDepth0, RWTexture2D<float> outDepth1, RWTexture2D<float> outDepth2, RWTexture2D<float> outDepth3, RWTexture2D<float> outDepth4)
{
    const uint2 baseCoord = dispatchThreadID;
    const uint2 pixCoord = baseCoord * 2;
    float4 depths4 = sourceNDCDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.w));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.z));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.y));
    outDepth0[pixCoord + uint2(0, 0)] = depth0;
    outDepth0[pixCoord + uint2(1, 0)] = depth1;
    outDepth0[pixCoord + uint2(0, 1)] = depth2;
    outDepth0[pixCoord + uint2(1, 1)] = depth3;

    float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3);
    outDepth1[baseCoord] = dm1;
    g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    [branch]
    if (all((groupThreadID.xy % 2) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
        float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

        float dm2 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth2[baseCoord / 2] = dm2;
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    [branch]
    if (all((groupThreadID.xy % 4) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
        float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

        float dm3 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth3[baseCoord / 4] = dm3;
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    [branch]
    if (all((groupThreadID.xy % 8) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
        float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth4[baseCoord / 8] = dm4;
    }
}

// ------------------------------------------------------------------------------------------------
// Edge Detection
// ------------------------------------------------------------------------------------------------

float4 XeGTAO_CalculateEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ)
{
    float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;

    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
    return saturate(1.25 - edgesLRTB * rcp(centerZ * 0.011));
}

float XeGTAO_PackEdges(float4 edgesLRTB)
{
    edgesLRTB = round(saturate(edgesLRTB) * 2.9);
    return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// ------------------------------------------------------------------------------------------------
// View-Space Position Reconstruction
// ------------------------------------------------------------------------------------------------

float3 XeGTAO_ComputeViewspacePosition(float2 screenPos, float viewspaceDepth)
{
    float3 ret;
    ret.xy = (NDC_TO_VIEW_MUL * screenPos.xy + NDC_TO_VIEW_ADD) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

// ------------------------------------------------------------------------------------------------
// Fast Math Approximations
// ------------------------------------------------------------------------------------------------

float XeGTAO_FastSqrt(float x)
{
    return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

float XeGTAO_FastACos(float inX)
{
    float x = abs(inX);
    float res = -0.156583 * x + XE_GTAO_PI_HALF;
    res *= XeGTAO_FastSqrt(1.0 - x);
    return inX >= 0 ? res : XE_GTAO_PI - res;
}

// ------------------------------------------------------------------------------------------------
// Main GTAO Pass
// ------------------------------------------------------------------------------------------------

void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
    float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

    float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
    float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

    float viewspaceZ = valuesUL.y;

    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    const float edges = XeGTAO_PackEdges(edgesLRTB);

    viewspaceZ *= 0.99999;

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    const float3 viewVec = normalize(-pixCenterPos);

    viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

    const float effectRadius = EFFECT_RADIUS * RADIUS_MULTIPLIER;
    const float sampleDistributionPower = SAMPLE_DISTRIBUTION_POWER;
    const float thinOccluderCompensation = THIN_OCCLUDER_COMPENSATION;

    // Squared distance falloff (UE4 style)
    const float falloffStart = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);
    const float falloffStartSq = falloffStart * falloffStart;
    const float falloffEndSq = effectRadius * effectRadius;
    const float falloffScale = 1.0 / (falloffEndSq - falloffStartSq);
    const float falloffBias = -falloffStartSq * falloffScale;

    float visibility = 0.0;

    {
        const float noiseSlice = localNoise.x;
        const float noiseSample = localNoise.y;

        const float pixelTooCloseThreshold = 1.3;

        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDC_TO_VIEW_MUL_X_PIXEL_SIZE;

        float screenspaceRadius = effectRadius * rcp(abs(pixelDirRBViewspaceSizeAtCenterZ.x));
        screenspaceRadius = clamp(screenspaceRadius, STEPS_PER_SLICE, XE_GTAO_MAX_PIXEL_RADIUS);

        visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

        const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

        for (float slice = 0.0; slice < SLICE_COUNT; slice++)
        {
            float sliceK = (slice + noiseSlice) / SLICE_COUNT;
            float phi = sliceK * XE_GTAO_PI;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2 omega = float2(cosPhi, -sinPhi);

            omega *= screenspaceRadius;

            const float3 directionVec = float3(cosPhi, sinPhi, 0.0);
            const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
            const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));
            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(projectedNormalVecLength));
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            float horizonCos0 = lowHorizonCos0;
            float horizonCos1 = lowHorizonCos1;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++)
            {
                const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482;
                float stepNoise = frac(noiseSample + stepBaseNoise);

                float s = (step + stepNoise) / STEPS_PER_SLICE;
                s = pow(s, sampleDistributionPower);
                s += minS;

                float2 sampleOffset = s * omega;
                float sampleOffsetLength = length(sampleOffset);

                const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, XE_GTAO_DEPTH_MIP_LEVELS);

                sampleOffset = round(sampleOffset) * VIEWPORT_PIXEL_SIZE;

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0);

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1);

                float3 sampleDelta0 = samplePos0 - pixCenterPos;
                float3 sampleDelta1 = samplePos1 - pixCenterPos;
                float sampleDist0 = length(sampleDelta0);
                float sampleDist1 = length(sampleDelta1);

                float3 sampleHorizonVec0 = sampleDelta0 * rcp(sampleDist0);
                float3 sampleHorizonVec1 = sampleDelta1 * rcp(sampleDist1);

                float3 adjustedDelta0 = float3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation));
                float3 adjustedDelta1 = float3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation));

                float distSq0 = dot(adjustedDelta0, adjustedDelta0);
                float distSq1 = dot(adjustedDelta1, adjustedDelta1);

                float atten0 = saturate(distSq0 * falloffScale + falloffBias);
                float atten1 = saturate(distSq1 * falloffScale + falloffBias);

                float weight0 = 1.0 - atten0;
                float weight1 = 1.0 - atten1;

                float shc0 = dot(sampleHorizonVec0, viewVec);
                float shc1 = dot(sampleHorizonVec1, viewVec);

                shc0 = lerp(lowHorizonCos0, shc0, weight0);
                shc1 = lerp(lowHorizonCos1, shc1, weight1);

                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);
            }

            projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);

            float h0 = -XeGTAO_FastACos(horizonCos1);
            float h1 = XeGTAO_FastACos(horizonCos0);

            float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
            float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
            float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
            visibility += localVisibility;
        }

        visibility /= SLICE_COUNT;
        visibility = pow(visibility, FINAL_VALUE_POWER);
        visibility = max(0.03, visibility);
    }

    visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
    outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
}

// ------------------------------------------------------------------------------------------------
// Denoise Pass
// ------------------------------------------------------------------------------------------------

void XeGTAO_DecodeGatherPartial(float4 packedValue, out float outDecoded[4])
{
    for (int i = 0; i < 4; i++)
    {
        outDecoded[i] = packedValue[i];
    }
}

float4 XeGTAO_UnpackEdges(float _packedVal)
{
    uint packedVal = uint(_packedVal * 255.5);
    float4 edgesLRTB;
    edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;
    edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
    edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
    edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

    return saturate(edgesLRTB);
}

void XeGTAO_AddSample(float ssaoValue, float edgeValue, inout float sum, inout float sumWeight)
{
    float weight = edgeValue;
    sum += weight * ssaoValue;
    sumWeight += weight;
}

void XeGTAO_Denoise(uint2 pixCoordBase, Texture2D sourceAOTermAndEdges, SamplerState texSampler, RWTexture2D<unorm float4> outputTexture, const uniform bool finalApply)
{
    const float blurAmount = finalApply ? DENOISE_BLUR_BETA : DENOISE_BLUR_BETA / 5.0;
    const float diagWeight = 0.85 * 0.5;

    float aoTerm[2];
    float4 edgesC_LRTB[2];
    float weightTL[2];
    float weightTR[2];
    float weightBL[2];
    float weightBR[2];

    const float2 gatherCenter = float2(pixCoordBase.x, pixCoordBase.y) * VIEWPORT_PIXEL_SIZE;
    float4 edgesQ0 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(0, 0));
    float4 edgesQ1 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(2, 0));
    float4 edgesQ2 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(1, 2));

    float visQ0[4];
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 0)), visQ0);
    float visQ1[4];
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 0)), visQ1);
    float visQ2[4];
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 2)), visQ2);
    float visQ3[4];
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 2)), visQ3);

    for (int side = 0; side < 2; side++)
    {
        const int2 pixCoord = int2(pixCoordBase.x + side, pixCoordBase.y);

        float4 edgesL_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.x : edgesQ0.y);
        float4 edgesT_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.z : edgesQ1.w);
        float4 edgesR_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ1.x : edgesQ1.y);
        float4 edgesB_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ2.w : edgesQ2.z);

        edgesC_LRTB[side] = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.y : edgesQ1.x);
        edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

        const float leak_threshold = 2.5;
        const float leak_strength = 0.5;
        float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
        edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);

        weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
        weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
        weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
        weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

        float ssaoValue = side == 0 ? visQ0[1] : visQ1[0];
        float ssaoValueL = side == 0 ? visQ0[0] : visQ0[1];
        float ssaoValueT = side == 0 ? visQ0[2] : visQ1[3];
        float ssaoValueR = side == 0 ? visQ1[0] : visQ1[1];
        float ssaoValueB = side == 0 ? visQ2[2] : visQ3[3];
        float ssaoValueTL = side == 0 ? visQ0[3] : visQ0[2];
        float ssaoValueBR = side == 0 ? visQ3[3] : visQ3[2];
        float ssaoValueTR = side == 0 ? visQ1[3] : visQ1[2];
        float ssaoValueBL = side == 0 ? visQ2[3] : visQ2[2];

        float sumWeight = blurAmount;
        float sum = ssaoValue * sumWeight;

        XeGTAO_AddSample(ssaoValueL, edgesC_LRTB[side].x, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueR, edgesC_LRTB[side].y, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueT, edgesC_LRTB[side].z, sum, sumWeight);
        XeGTAO_AddSample(ssaoValueB, edgesC_LRTB[side].w, sum, sumWeight);

        XeGTAO_AddSample(ssaoValueTL, weightTL[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueTR, weightTR[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueBL, weightBL[side], sum, sumWeight);
        XeGTAO_AddSample(ssaoValueBR, weightBR[side], sum, sumWeight);

        aoTerm[side] = sum * rcp(sumWeight);
        aoTerm[side] *= finalApply ? XE_GTAO_OCCLUSION_TERM_SCALE : 1.0;
        outputTexture[pixCoord] = float4(aoTerm[side], 0.0, 0.0, 0.0);
    }
}

#endif // __XE_GTAO_HLSLI__