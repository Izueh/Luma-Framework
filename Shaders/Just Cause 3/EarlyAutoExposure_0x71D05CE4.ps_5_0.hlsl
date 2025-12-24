#include "Includes/Common.hlsl"

cbuffer cbInstanceConsts : register(b1)
{
  float4 InstanceConsts[2] : packoffset(c0);
}

#ifndef FORCE_VANILLA_AUTO_EXPOSURE_TYPE
#define FORCE_VANILLA_AUTO_EXPOSURE_TYPE 0
#endif // FORCE_VANILLA_AUTO_EXPOSURE_TYPE

SamplerState SourceImage_s : register(s0);
SamplerState LumaLinearSampler : register(s15);
Texture2D<float4> SourceImage : register(t0);

// This is run before post processing and runs just like the late auto exposure, though it doesn't seem to do anything.
// Note that this seems to run at 30Hz or so, or anyway it seems to ping pong frame by frame between early and late auto exposure.
// This might be used to primarily influence bloom, which is seemengly affected by exposure twice.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float oDepth : SV_Depth)
{
  float4 r0;
#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 2
  r0.xyz = SourceImage.Sample(SourceImage_s, v1.xy).xyz;
#else
  v1.xy = v0.xy / float2(320, 180); // Reconstruct the UV so it includes the top part of the image too (this is also important because we calculated the optimal mip of the full screen image)
  r0.xyz = SourceImage.Sample(LumaLinearSampler, v1.xy).xyz;
#endif

#if 1 // Luma: Fix Nans and remove negative values (they'd be trash)
  r0.xyz = max(r0.xyz, 0.0);
#endif

  r0.xyz = InstanceConsts[0].y * r0.xyz;

#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 1
  r0.x = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
#else
  r0.x = GetLuminance(r0.xyz);
#endif

#if DISABLE_AUTO_EXPOSURE
  r0.x = 0.5;
#endif

  oDepth = saturate(InstanceConsts[0].x * r0.x);
}