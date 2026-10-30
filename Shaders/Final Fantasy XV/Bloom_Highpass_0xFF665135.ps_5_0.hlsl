// ---- Created with 3Dmigoto v1.4.1 on Mon May  4 16:47:23 2026

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
  out float4 o0 : SV_TARGET0)
{
  // Positive tent filter covers the quarter-resolution downsample footprint.
  float2 uv = (v1.xy + v2.zw) * 0.5;
  float2 pixelOffset = (v1.zw - v2.xy) * 0.5;
  float3 color = srcSamplerTexture.SampleLevel(srcSampler_s, uv, 0).xyz * 0.125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2(-2, -2), 0).xyz * 0.03125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2( 2, -2), 0).xyz * 0.03125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2(-2,  2), 0).xyz * 0.03125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2( 2,  2), 0).xyz * 0.03125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2(-2,  0), 0).xyz * 0.0625;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2( 2,  0), 0).xyz * 0.0625;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2( 0, -2), 0).xyz * 0.0625;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, uv + pixelOffset * float2( 0,  2), 0).xyz * 0.0625;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, v1.xy, 0).xyz * 0.125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, v1.zw, 0).xyz * 0.125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, v2.xy, 0).xyz * 0.125;
  color += srcSamplerTexture.SampleLevel(srcSampler_s, v2.zw, 0).xyz * 0.125;

  float luma = max(9.99999975e-06, dot(color, float3(0.298999995, 0.587000012, 0.114)));
  color *= max(9.99999975e-06, luma - highpass_threshold.x) / luma;
  o0.xyz = exp2(highpass_gamma.xyz * log2(color));
  o0.w = 0;
}