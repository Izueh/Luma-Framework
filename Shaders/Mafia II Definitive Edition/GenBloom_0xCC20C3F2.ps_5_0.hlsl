#include "../Includes/Common.hlsl"

#ifndef ENABLE_IMPROVED_BLOOM
#define ENABLE_IMPROVED_BLOOM 1
#endif

cbuffer _Globals : register(b0)
{
  row_major float4x4 g_SMapTM[4] : packoffset(c101);
  float4 g_DbgColor : packoffset(c117);
  float4 g_FilterTaps[8] : packoffset(c118);
  float4 g_FadingParams : packoffset(c126);
  float4 g_CSMRangesSqr : packoffset(c127);
  float2 g_SMapSize : packoffset(c128);
  float4 g_CameraOrigin : packoffset(c129);
  float4 MaxValueIntensity : packoffset(c0);
}

SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0;

// Luma: bloom was downscaled to an image of size.xy/16 (from mip 0 to mip 4), but with a linear sampler, which at best allows a 2x reduction, meaning the 8x reduction would be left "uncovered" and we'd leave ton of samples out, making bloom very low quality and unstable
// TODO: just make 4 mips of the source texture and sample that... Also I'm not 100% certain the scale is always 16x. As of now we still skip some samples, so it's not perfect, but it shall do!!!
#if ENABLE_IMPROVED_BLOOM

  // Centers of four 4-texel-wide sub-blocks within a 16-texel span:
  // (-6,-2,+2,+6) texels from the block center (covers [-8..+8) reasonably well).
  const float2 o00 = float2(-6.0, -6.0) * LumaSettings.SwapchainInvSize;
  const float2 o10 = float2(-2.0, -6.0) * LumaSettings.SwapchainInvSize;
  const float2 o20 = float2( 2.0, -6.0) * LumaSettings.SwapchainInvSize;
  const float2 o30 = float2( 6.0, -6.0) * LumaSettings.SwapchainInvSize;

  const float2 o01 = float2(-6.0, -2.0) * LumaSettings.SwapchainInvSize;
  const float2 o11 = float2(-2.0, -2.0) * LumaSettings.SwapchainInvSize;
  const float2 o21 = float2( 2.0, -2.0) * LumaSettings.SwapchainInvSize;
  const float2 o31 = float2( 6.0, -2.0) * LumaSettings.SwapchainInvSize;

  const float2 o02 = float2(-6.0,  2.0) * LumaSettings.SwapchainInvSize;
  const float2 o12 = float2(-2.0,  2.0) * LumaSettings.SwapchainInvSize;
  const float2 o22 = float2( 2.0,  2.0) * LumaSettings.SwapchainInvSize;
  const float2 o32 = float2( 6.0,  2.0) * LumaSettings.SwapchainInvSize;

  const float2 o03 = float2(-6.0,  6.0) * LumaSettings.SwapchainInvSize;
  const float2 o13 = float2(-2.0,  6.0) * LumaSettings.SwapchainInvSize;
  const float2 o23 = float2( 2.0,  6.0) * LumaSettings.SwapchainInvSize;
  const float2 o33 = float2( 6.0,  6.0) * LumaSettings.SwapchainInvSize;

  float3 sum = 0.0;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o00).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o10).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o20).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o30).xyz;

  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o01).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o11).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o21).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o31).xyz;

  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o02).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o12).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o22).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o32).xyz;

  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o03).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o13).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o23).xyz;
  sum += TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy + o33).xyz;

  r0.xyz = sum * (1.0 / 16.0);

#else

  r0.xyz = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyz;

#endif // ENABLE_IMPROVED_BLOOM

  r0.w = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_POSITIVE))); // Luma: calc luminance in linear space
#if 1 // Luma: clamp luminance to 1 as the source color would have been 0-1 UNORM anyway, and the multiple squares below go to INF or something
  r0.w = saturate(r0.w);
#endif
  r0.w = r0.w * r0.w; // ~Linear
  r0.w = r0.w * r0.w; // Linear Linear?
  r0.w = r0.w * r0.w; // Linear Linear Linear?
  r0.xyz *= r0.w;
  o0.xyz = min(MaxValueIntensity.x, r0.xyz * MaxValueIntensity.y); // Max x defaults to 0.6, and Max y defaults to 1, so anything beyond 0.6 was clipped away from bloom (weird choice, you'd expect it to be the opposite)
  o0.w = 1;
}