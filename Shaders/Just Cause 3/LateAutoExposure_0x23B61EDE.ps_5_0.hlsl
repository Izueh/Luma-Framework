#include "Includes/Common.hlsl"

cbuffer cbInstanceConsts : register(b1)
{
  float4 InstanceConsts[2] : packoffset(c0);
}

// It kinda looks better with this disabled as the game is over exposed, both washed out and deep fried and crushed at the same too
#ifndef FORCE_VANILLA_AUTO_EXPOSURE_TYPE
#define FORCE_VANILLA_AUTO_EXPOSURE_TYPE 0
#endif // FORCE_VANILLA_AUTO_EXPOSURE_TYPE

#ifndef DISABLE_AUTO_EXPOSURE
#define DISABLE_AUTO_EXPOSURE 0
#endif

SamplerState SourceImage_s : register(s0);
SamplerState LumaLinearSampler : register(s15);
Texture2D<float4> SourceImage : register(t0); // Nearest sampler

// Funny enough, exposure is written as depth.
// Also, funny enough 2, this skips the top portion of the image as is always a 16:9 texture.
// Note that this seems to run at 30Hz or so, or anyway it seems to ping pong frame by frame between early and late auto exposure.
// TODO: the auto exposure speed is dependent on FPS, meaning it changes much more rapidly at higher frame rates, which is quite noticeable, especially in the sky (fixed now?). We should either blend the exposure variables with their history in the TM and bloom shaders, or blend the output of the two auto exposure shaders with a history to emulate 60fps. The problems are camera cuts for both. Alternatively we could skip running this shader if it runs beyond 60Hz, but i don't think that'd help.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float oDepth : SV_Depth)
{
  float4 r0;
#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 2
  r0.xyz = SourceImage.Sample(SourceImage_s, v1.xy).xyz; // This caused huge flickers when there's a small strong light on screen, so we pre-calculated mips up to 1x with Luma so it's stable (the alternative would have been area box sampling, to cover all pixels)
#else
  v1.xy = v0.xy / float2(320, 180); // Reconstruct the UV so it includes the top part of the image too (this is also important because we calculated the optimal mip of the full screen image)
  r0.xyz = SourceImage.Sample(LumaLinearSampler, v1.xy).xyz;
#endif

#if 1 // Luma: improve auto exposure calculations
  
#if 0
  r0.xyz = saturate(r0.xyz);
#elif FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 2
  // Quick tonemap to SDR
  r0.xyz /= max(1.0, max3(r0.xyz));
#endif

#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 1
  r0.x = average(r0.xyz);
#else // Probably looks very different on green/grass and blue/sky, but perceptually it's more accurate and that's what matters
  r0.x = GetLuminance(r0.xyz, GCT_POSITIVE);
#endif

#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 2 && 0 // Note: is this even really necessary? Maybe not! Especially given that "InstanceConsts[0].x" is almost always 1
  r0.x = saturate(r0.x); // Preserve SDR look, otherwise in HDR exposure goes crazy
#endif

#else // OG: average rgb in gamma space
  r0.x = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
#endif

#if DISABLE_AUTO_EXPOSURE // Mostly for testing, but can also provide a more HDR look (dark stays dark, bright stays bright)
  r0.x = 0.5; // Default to the most sensible value, or anyway a value that looks good (we could go even lower)
#endif

  oDepth = saturate(InstanceConsts[0].x * r0.x); // Saturate here is redundant? Or even obsolete? It's written on UNORM depth, so yes it's useless
}