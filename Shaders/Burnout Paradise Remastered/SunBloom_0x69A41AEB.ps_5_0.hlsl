#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 kColourAndPower : packoffset(c0);
}

SamplerState OcclusionSource_s : register(s0);
Texture2D<float4> OcclusionSourceTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
#if 1 // Luma: fix sun being stretched in UW (it was only drawn round or so at 16:9)
  float targetAspectRatio = LumaSettings.GameSettings.InvRenderRes.y / LumaSettings.GameSettings.InvRenderRes.x;
  v1.x *= max(targetAspectRatio / (16.0 / 9.0), 1.0);
#endif
  r0.x = dot(v1.xy, v1.xy);
  r0.x = 1 - r0.x;
  r0.x = max(0, r0.x);
  r0.x = r0.x * r0.x;
  o0.xyz = kColourAndPower.xyz * r0.x;
  o0.w = OcclusionSourceTexture.Sample(OcclusionSource_s, float2(0,0)).x * LumaSettings.GameSettings.BloomIntensity * 2.0; // Intensity based on how occluded the sun was
#if 1 // Bloom in the mod defaults to 50% (given it's too strong for a modern HDR look), but the sun bloom we want it at the original intensity (or actually, more), so link it with the HDR boost intensity too, which defaults to 1
  float2 uv = v0.xy * 2.0 * LumaSettings.GameSettings.InvRenderRes; // Scale by 2 as this happens at half res
  bool forceVanillaSDR = ShouldForceSDR(uv);
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
    o0.xyz *= 1.0 + LumaSettings.GameSettings.HDRBoostIntensity * 1.0;
#endif
}