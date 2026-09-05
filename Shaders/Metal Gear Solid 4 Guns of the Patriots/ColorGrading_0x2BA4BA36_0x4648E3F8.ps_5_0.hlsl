#include "../Includes/Common.hlsl"

#if _2BA4BA36
#define HAS_COLOR_PALETTE 1
#endif

#ifndef HAS_COLOR_PALETTE
#define HAS_COLOR_PALETTE 0
#endif

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

Texture2D<float4> SceneTexture    : register(t0);
Texture2D<float4> LUTIndexTexture : register(t1);
Texture2D<float4> ColorLUT        : register(t2);

SamplerState SceneSampler    : register(s0);
SamplerState LUTIndexSampler : register(s1);
SamplerState ColorLUTSampler : register(s2);

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

// -----------------------------------------------------------------------------
// Applies the 64x64 2D LUT independently to R, G and B.
//
// X = input channel value
// Y = LUT/palette selector
// -----------------------------------------------------------------------------
float3 ApplyColorLUT(float3 color, float lutY)
{
    static const float LUTSize  = 64.0; // TODO: dynamic value?
    static const float LUTScale = (LUTSize - 1.0) / LUTSize; // 63 / 64
    static const float LUTBias  = 0.5 / LUTSize;             // 0.5 / 64

#if ENABLE_LUMA // Luma: fix missing half texel offset for LUT // TODO: handle raised shadow
    // Convert logical [0,1] LUT coordinates to texel centers.
    color = color * LUTScale + LUTBias;
    lutY  = lutY  * LUTScale + LUTBias;
#endif

    float3 result;
    result.r = ColorLUT.Sample(ColorLUTSampler, float2(color.r, lutY)).r;
    result.g = ColorLUT.Sample(ColorLUTSampler, float2(color.g, lutY)).r;
    result.b = ColorLUT.Sample(ColorLUTSampler, float2(color.b, lutY)).r;
    return result;
}

void main(
    float4 position      : SV_POSITION,
    float4 vertexColor   : COLOR0,
    float2 sceneUV       : TEXCOORD0,
    float4 vignetteCoord : TEXCOORD1,
    out float4 output    : SV_TARGET0)
{
    // -------------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------------

    const float3 colorScale       = cb0[0].xyz;
    const float  desaturation     = cb0[0].w;

    const float  contrast         = cb0[1].x;
    const float  brightnessOffset = cb0[1].y;

    const float3 colorMin         = cb0[2].xyz;
    const float3 colorMax         = cb0[3].xyz;

    const float2 lutIndexOffset   = cb0[4].xy;
    const float  lutIndexScale    = cb0[4].w;

    const float3 fadeColor        = cb0[5].xyz;
    const float  fadeBlend        = cb0[5].w;

    const float  vignetteScale    = cb0[6].x;
    const float  vignetteBias     = cb0[6].y;

    const float2 renderSize       = cb0[8].xy;

    // -------------------------------------------------------------------------
    // LUT selector
    //
    // t1 provides the Y coordinate used when looking up the 2D color LUT.
    // -------------------------------------------------------------------------

    float2 lutIndexUV = position.xy * (float2(16.0, 9.0) / renderSize); // TODO: aspect ratio scaling?
    lutIndexUV = lutIndexUV * 0.5 + lutIndexOffset;

    float lutY = LUTIndexTexture.Sample(LUTIndexSampler, lutIndexUV).r;
    lutY *= lutIndexScale;

    // -------------------------------------------------------------------------
    // Scene color
    // -------------------------------------------------------------------------

    float3 sceneColor = SceneTexture.Sample(SceneSampler, sceneUV).rgb;
#if !ENABLE_LUMA // Emulate UNORM
    sceneColor = saturate(sceneColor);
    // TODO: add hue emulation?
#endif

    // -------------------------------------------------------------------------
    // Per-channel 2D color LUT / palette
    // -------------------------------------------------------------------------

#if HAS_COLOR_PALETTE
    // TODO: quick remap to avoid clamping
    float sceneColorMax = max(max3(sceneColor), 1.0);
    float3 color = ApplyColorLUT(sceneColor / sceneColorMax, lutY) * sceneColorMax;
#else
    float3 color = sceneColor;
#endif

    // -------------------------------------------------------------------------
    // Saturation
    // -------------------------------------------------------------------------

#if ENABLE_LUMA && 0 // TODO: desat in linear (not always better actually)

    color = gamma_to_linear(color, GCT_MIRROR);

#if HAS_COLOR_PALETTE
    float luminance = GetLuminance(color.yxz); // TODO: calc in linear etc?
#else
    float luminance = GetLuminance(color.xyz);
#endif
    color = lerp(color, luminance, desaturation);

    color = linear_to_gamma(color, GCT_MIRROR);

#else

#if HAS_COLOR_PALETTE
    float luminance = dot(color.yxz, float3(0.300000012, 0.589999974, 0.109999999));
#else
    float luminance = dot(color, float3(0.300000012, 0.589999974, 0.109999999));
#endif
    color = lerp(color, luminance, desaturation);

#endif

    // -------------------------------------------------------------------------
    // Color scaling
    // -------------------------------------------------------------------------

    color *= colorScale;

    // -------------------------------------------------------------------------
    // Contrast + brightness
    // -------------------------------------------------------------------------

    float contrastMidPoint = 0.5;
#if ENABLE_LUMA // Luma modern contrast method that doesn't raise blacks not generate invalid colors // TODO: test
#if DEVELOPMENT
    if (DVS1)
    {
        color = ((color - contrastMidPoint) * contrast) + contrastMidPoint;
        color += brightnessOffset;
    }
    else
#endif
    {
	// Empirical value to match the original game constrast formula look more.
	// This has been carefully researched and applies to both positive and negative contrast.
	const float adjustedContrast = pow(contrast, 1.5);
	// Do abs() to avoid negative power, even if it doesn't make 100% sense, these formulas are fine as long as they look good
	color = pow(abs(color) / contrastMidPoint, adjustedContrast) * contrastMidPoint * sign(color);
    
    // Only add negative offsets, as they are used in black and white cutscenes etc
    color += min(brightnessOffset, 0.0);
    }
#else
    color = ((color - contrastMidPoint) * contrast) + contrastMidPoint;
    
    // This was mostly used to compensate contrast generating negative values.
    color += brightnessOffset; // TODO: improve
#endif

    // -------------------------------------------------------------------------
    // Clamp ranges
    // -------------------------------------------------------------------------

    color = max(colorMin, color); // TODO: handle it! And the max too in case it's not 1 1 1
#if 0 // Luma HDR
    color = min(colorMax, color);
#endif

#if ENABLE_VIGNETTE
    // -------------------------------------------------------------------------
    // Radial vignette
    // -------------------------------------------------------------------------

    float radius = length(vignetteCoord.xy);
    float vignette = saturate(radius * vignetteScale + vignetteBias);
    vignette = pow(vignette, 1.4);
    // Original shader uses the inverse as the scene contribution.
    float sceneWeight = 1.0 - vignette;
    color *= sceneWeight;
#endif

    // -------------------------------------------------------------------------
    // fade
    // -------------------------------------------------------------------------

    color = lerp(color, fadeColor, fadeBlend); // TODO: improve

    output = float4(color, 1.0);
}