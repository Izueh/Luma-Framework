#include "Includes/Common.hlsl"

cbuffer cbInstanceConsts : register(b1)
{
  float4 InstanceConsts[2] : packoffset(c0);
}

// It kinda looks better with this disabled as the game is over exposed, both washed out and deep fried and crushed at the same too
#ifndef FORCE_VANILLA_AUTO_EXPOSURE
#define FORCE_VANILLA_AUTO_EXPOSURE 0
#endif // FORCE_VANILLA_AUTO_EXPOSURE

SamplerState SourceImage_s : register(s0);
Texture2D<float4> SourceImage : register(t0); // Nearest sampler

// Funny enough, exposure is written as depth.
// Also, funny enough 2, this skips the top portion of the image as is always a 16:9 texture.
// TODO: test if the auto exposure speed is adapted by FPS, because it often changes rapidly, which is very noticeable, especially in the sky
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float oDepth : SV_Depth)
{
  float4 r0;
  r0.xyz = SourceImage.Sample(SourceImage_s, v1.xy).xyz; // TODO: use linear sampler and possibly box area downsampling to cover the whole range (even if slow...), or maybe call "generate mips" in DX11. This causes huge flickers when there's a small strong light on screen.

#if 1 // Luma: improve auto exposure calculations
  
#if 0
  r0.xyz = saturate(r0.xyz);
#elif FORCE_VANILLA_AUTO_EXPOSURE
  // Quick tonemap to SDR
  r0.xyz /= max(1.0, max3(r0.xyz));
#endif

#if 0 // Probably looks too different on green/grass, even if perceptually it'd be more "accurate"
  r0.x = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_POSITIVE)));
#else
  r0.x = average(r0.xyz);
#endif

#if 0 // FORCE_VANILLA_AUTO_EXPOSURE // TODO: is this even really necessary? Maybe not! Especially given that "InstanceConsts[0].x" is almost always 1
  r0.x = saturate(r0.x); // Preserve SDR look, otherwise in HDR exposure goes crazy
#endif

#else // OG: average rgb in gamma space
  r0.x = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
#endif

  oDepth = saturate(InstanceConsts[0].x * r0.x); // Saturate here is redundant? Or even obsolete? It's written on UNORM depth, so yes it's useless
  
#if 0 // Test: disable auto exposure
  oDepth = 0.333;
#endif
}