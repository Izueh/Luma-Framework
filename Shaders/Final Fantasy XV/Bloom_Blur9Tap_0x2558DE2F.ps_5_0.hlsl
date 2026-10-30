// ---- Refactored from 3Dmigoto decompile
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
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float2 v5 : TEXCOORD4,
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
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v1.xy, minUV, maxUV), 0).xyz * gauss_weights[0].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v1.zw, minUV, maxUV), 0).xyz * gauss_weights[1].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v2.xy, minUV, maxUV), 0).xyz * gauss_weights[2].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v2.zw, minUV, maxUV), 0).xyz * gauss_weights[3].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v3.xy, minUV, maxUV), 0).xyz * gauss_weights[4].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v3.zw, minUV, maxUV), 0).xyz * gauss_weights[5].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v4.xy, minUV, maxUV), 0).xyz * gauss_weights[6].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v4.zw, minUV, maxUV), 0).xyz * gauss_weights[7].xyz;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v5.xy, minUV, maxUV), 0).xyz * gauss_weights[8].xyz;

  o0.xyz = gauss_mix.xyz * blurColor;
  o0.w = 0.0;
}