#define LUT_3D 1

#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer _Globals : register(b0)
{
  float ImageSpace_hlsl_ToneMapPixelShader0000000000000000000000000000000b_4bits : packoffset(c0) = {0};
  float4 fogColor : packoffset(c1);
  float3 fogTransform : packoffset(c2);
  float2 fogLuminance : packoffset(c3);
  row_major float3x4 screenDataToCamera : packoffset(c4);
  float globalScale : packoffset(c7);
  float sceneDepthAlphaMask : packoffset(c7.y);
  float globalOpacity : packoffset(c7.z);
  float distortionBufferScale : packoffset(c7.w);
  float3 wToZScaleAndBias : packoffset(c8);
  float4 screenTransform[2] : packoffset(c9);
  float4 textureToPixel : packoffset(c11);
  float4 pixelToTexture : packoffset(c12);
  float maxScale : packoffset(c13) = {0};
  float bloomAlpha : packoffset(c13.y) = {0};
  float sceneBias : packoffset(c13.z) = {1};
  float3 gammaSettings : packoffset(c14); // This seems to be 1 at all times, it's unused
  float exposure : packoffset(c14.w) = {0};
  float deltaExposure : packoffset(c15) = {0};
  float4 ColorFill : packoffset(c16);
  float2 LowResTextureDimensions : packoffset(c17);
  float2 DownsizeTextureDimensions : packoffset(c17.z);
}

SamplerState s_framebuffer_s : register(s0); // Point sampler
SamplerState s_bloom_s : register(s1); // Linear sampler
SamplerState s_toneMapTable_s : register(s2); // Linear sampler
Texture2D<float4> s_framebuffer : register(t0);
Texture2D<float4> s_bloom : register(t1);
Texture3D<float4> s_toneMapTable : register(t2); // LUT

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1;
  
  r1.xyzw = s_framebuffer.Sample(s_framebuffer_s, v1.xy).xyzw;

  r0.xyzw = s_bloom.Sample(s_bloom_s, w1.xy).xyzw;
  r1.xyz += r0.xyz * bloomAlpha * LumaSettings.GameSettings.BloomIntensity;
  
  outColor.w = saturate(r1.w); // Keep the saturate on alpha, it's probably pointless anyway
  r0.xyz = r1.xyz * sceneBias; // Exposure // Luma: removed a saturate from here

#if ENABLE_COLOR_GRADING && !ENABLE_LUMA // Vanilla

  float3 clippedLuminance = r0.xyz - saturate(r0.xyz);
  r0.xyz = pow(abs(r0.xyz), 1.0 / 2.2) * sign(r0.xyz); // Linear to gamma
  outColor.xyz = s_toneMapTable.Sample(s_toneMapTable_s, saturate(r0.xyz)).xyz; // Broken sampling math, here we have not corrected this, it clips!
  outColor.xyz = pow(abs(outColor.xyz), 2.2) * sign(outColor.xyz); // Gamma to linear
#if ENABLE_LUMA && 0 // Luma: cheap awful trick to recover luminance
  outColor.xyz += clippedLuminance;
#endif

#else // !ENABLE_COLOR_GRADING || ENABLE_LUMA

// LUTs were clipped around the first and half texel due to bad sampling math. This will emulate the shadow darkening and highlight brightening from it (contrast boost), without causing clipping.
// The original error applied in gamma space but we do it in linear.
#if ENABLE_LUMA && LUT_SAMPLING_ERROR_EMULATION_MODE > 0
  float clippedAmount = 0.5 / 16.0; // The first and last half texels of the LUT were clipped away (in gamma space)

  float3 previousColor = r0.rgb;
  
  // Adjust params for shadows
  // Empyrically found values that look good
  float adjustmentScale = 0.333; // Basically the added contrast curve strength
  float adjustmentRange = 1.0 / 3.0; // Theoretically the added shadow crush would have happened until 0.5 (in gamma space, and ~0.218 in linear), by then it would have faded out (and highlights clipping would have begun, but we don't simulate that)
  float adjustmentPow = 1.0; // Values > 1 might look good too, this kinda controls the "smoothness" of the contrast curve
#if LUT_SAMPLING_ERROR_EMULATION_MODE != 2 // Per channel (it looks nicer)
  r0.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma(previousColor, GCT_POSITIVE) / adjustmentRange, adjustmentPow)));
#else // LUT_SAMPLING_ERROR_EMULATION_MODE == 2 // By luminance
  r0.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma1(max(GetLuminance(previousColor), 0.0)) / adjustmentRange, adjustmentPow)));
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 2

#if LUT_SAMPLING_ERROR_EMULATION_MODE != 3
  // Adjust params for highlights
  adjustmentScale = 1.0 - adjustmentScale; // Flip it because it looks good like this
  adjustmentRange = 1.0 - adjustmentRange; // Do the remaining range
  float3 highlightsPerChannelScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma(previousColor, GCT_SATURATE)) / adjustmentRange, adjustmentPow)));
  float highlightsByLuminanceScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma1(saturate(GetLuminance(previousColor)))) / adjustmentRange, adjustmentPow)));
#if 0 // Per channel (looks deep fried)
  r0.rgb *= highlightsPerChannelScale;
#elif 0 // By luminance (looks like AutoHDR)
  r0.rgb *= highlightsByLuminanceScale;
#else // Mixed (looks best on highlights)
  r0.rgb *= lerp(highlightsPerChannelScale, highlightsByLuminanceScale, 0.75);
#endif
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 3
#endif // ENABLE_LUMA && LUT_SAMPLING_ERROR_EMULATION_MODE > 0

#if ENABLE_COLOR_GRADING && ENABLE_LUT_EXTRAPOLATION // HDR LUT Extrapolation (most LUTs seem to be neutral in this game)

  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = r0.rgb;
  extrapolationData.vanillaInputColor = saturate(r0.rgb); // An estimate of the SDR value

  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
  extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
  extrapolationSettings.extrapolationQuality = 2;
  extrapolationSettings.samplingQuality = 2;
  extrapolationSettings.lutSize = 0; // Automatically determine it
    
  extrapolationSettings.inputLinear = true;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = false;
  extrapolationSettings.outputLinear = true;
  
  // Basically a highlights desaturation setting. Expose if needed, but highlights look good saturated in this game, as that was seemengly the artistic intent.
  extrapolationSettings.vanillaLUTRestorationAmount = 0.0;

  // Intermediary gamma correction through LUT
  extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
  extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;

  outColor.rgb = SampleLUTWithExtrapolation(s_toneMapTable, s_toneMapTable_s, extrapolationData, extrapolationSettings);
  
#else // !ENABLE_COLOR_GRADING || !ENABLE_LUT_EXTRAPOLATION // Skip LUT

  // Nothing to do, game was already in linear
  outColor.rgb = r0.xyz;

#endif // ENABLE_COLOR_GRADING && ENABLE_LUT_EXTRAPOLATION

#endif // ENABLE_COLOR_GRADING && !ENABLE_LUMA
  
  outColor.xyz = pow(abs(outColor.xyz), 1.0 / 2.2) * sign(outColor.xyz); // Linear to gamma (the game used 2.2)
}