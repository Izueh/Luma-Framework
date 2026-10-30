#ifndef BLOOM_TAP_COUNT
#error BLOOM_TAP_COUNT must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#ifndef BLOOM_OFFSET_STRIDE
#error BLOOM_OFFSET_STRIDE must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#ifndef BLOOM_UV_SEMANTIC
#error BLOOM_UV_SEMANTIC must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 graph_color : packoffset(c0);
  float3 copy_srcColorFactor : packoffset(c1);
  float3 copy_srcColorFactorArray[11] : packoffset(c2);
  float3 highpass_gamma : packoffset(c13);
  float2 highpass_pixelOffset : packoffset(c14);
  float3 highpass_threshold : packoffset(c15);
  float2 gauss_minUV : packoffset(c16);
  float2 gauss_maxUV : packoffset(c16.z);
  float3 gauss_mix : packoffset(c17);
  float4 gauss_weights[65] : packoffset(c18);
  float2 gauss_offsets[65] : packoffset(c83);
  float3 compo_sparkBlend : packoffset(c148);
  float3 compo_oneMinusSparkBlend : packoffset(c149);
  float3 compo_glareGamma : packoffset(c150);
  float4 compo_glareWeights[11] : packoffset(c151);
  float4 compo_glareWeightsSumInv : packoffset(c162);
  float compo_glareBaseBlurMip : packoffset(c163);
  float3 compo_glareSoftAmount : packoffset(c163.y);
  float3 compo_glareSoftExpand : packoffset(c164);
  float3 compo_glareFoggyAmount : packoffset(c165);
  float3 compo_glareFoggyExpand : packoffset(c166);
  float4 compo_vignetteParam0 : packoffset(c167);
  float4 compo_vignetteParam1 : packoffset(c168);
  float4 compo_vignetteParam2 : packoffset(c169);
}

SamplerState srcSampler_s : register(s0);
Texture2D<float4> srcSamplerTexture : register(t0);

void main(
  float4 v0 : SV_POSITION0,
#if BLOOM_TAP_COUNT == 3
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
#elif BLOOM_TAP_COUNT == 5
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
#else
  float2 v1 : BLOOM_UV_SEMANTIC,
#endif
  out float4 o0 : SV_TARGET0)
{
  float2 minUV = gauss_minUV.xy;
  float2 maxUV = gauss_maxUV.xy;

  if (LumaData.GameData.IsUpscaling != 0)
  {
    minUV *= LumaData.RenderResolutionScale.xy;
    maxUV = 1.0 - (1.0 - maxUV) * LumaData.RenderResolutionScale.xy;
  }

  float3 blurColor = 0.0;

#if BLOOM_TAP_COUNT == 3 || BLOOM_TAP_COUNT == 5
#if BLOOM_TAP_COUNT == 3
  float2 sampleUVs[3] = { v1.xy, v1.zw, v2.xy };
#else
  float2 sampleUVs[5] = { v1.xy, v1.zw, v2.xy, v2.zw, v3.xy };
#endif
  float3 weightSum = 0.0;
  float3 nativeWeightSum = 0.0;
  float3 variance = 0.0;
  float2 centerUV = sampleUVs[BLOOM_TAP_COUNT / 2];
  float2 kernelSpan = sampleUVs[BLOOM_TAP_COUNT - 1] - sampleUVs[0];
  float2 axis = abs(kernelSpan.x) >= abs(kernelSpan.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
  [unroll]
  for (int tap = 0; tap < BLOOM_TAP_COUNT; tap++)
  {
    nativeWeightSum += gauss_weights[tap].xyz;
    float offset = dot(sampleUVs[tap] - centerUV, axis);
    variance += gauss_weights[tap].xyz * offset * offset;
  }
  variance /= max(nativeWeightSum, 1e-6);
  float radius = 3.0 * sqrt(max(variance.x, max(variance.y, variance.z)));

  // Match the native kernel's weighted spread and RGB gain with a smooth Gaussian.
  const int tapCount = (BLOOM_TAP_COUNT - 1) * 8 + 1;
  [unroll]
  for (int tap = 0; tap < tapCount; tap++)
  {
    const float offset = radius * (2.0 * tap / (tapCount - 1) - 1.0);
    const float2 sampleUV = clamp(centerUV + axis * offset, minUV, maxUV);
    float3 weight = exp2(-0.72134752 * offset * offset / max(variance, 1e-12));
    if (tap == 0 || tap == tapCount - 1)
      weight *= 0.5;
    blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, sampleUV, 0).xyz * weight;
    weightSum += weight;
  }
  blurColor *= nativeWeightSum / max(weightSum, 1e-6);
#else
  [unroll]
  for (int tap = 0; tap < BLOOM_TAP_COUNT; tap++)
  {
    const float2 sampleUV = clamp(v1.xy + gauss_offsets[tap * BLOOM_OFFSET_STRIDE].xy, minUV, maxUV);
    blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, sampleUV, 0).xyz * gauss_weights[tap].xyz;
  }
#endif

  o0.xyz = gauss_mix.xyz * blurColor;
  o0.w = 0.0;
}
