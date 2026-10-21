// ---- Created with 3Dmigoto v1.4.1 on Fri Oct 16 23:17:54 2026
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
Texture2D<float4> compo_glareSamplerTexture : register(t0);
Texture2D<float4> compo_sparkSamplerTexture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float3 glare = compo_glareSamplerTexture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  glare = exp2(compo_glareGamma.xyz * log2(compo_glareWeights[1].xyz * glare));
  float3 glareWeight = compo_glareWeightsSumInv.xyz * compo_oneMinusSparkBlend.xyz;
  glare *= glareWeight;

  float3 spark = compo_sparkSamplerTexture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  float3 bloom = glare + spark * compo_sparkBlend.xyz;

  float radiusSquared = dot(w1.xy, w1.xy);
  float vignette = radiusSquared * compo_vignetteParam2.y + compo_vignetteParam2.z;
  vignette = compo_vignetteParam2.z / vignette;
  vignette = vignette * vignette * compo_vignetteParam2.x - compo_vignetteParam2.x;
  float3 vignetteFactor = max(0, vignette * compo_vignetteParam1.xyz * LumaSettings.GameSettings.VignetteStrength + 1);

  o0 = float4(vignetteFactor * bloom * BLOOM_STRENGTH, 0);
  o1 = float4(vignetteFactor * glareWeight, 0);
}
