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
  r0.x = dot(v1.xy, v1.xy); // Note: this is probably not particularly UW friendly
  r0.x = 1 - r0.x;
  r0.x = max(0, r0.x);
  r0.x = r0.x * r0.x;
  o0.xyz = kColourAndPower.xyz * r0.x;
  o0.w = OcclusionSourceTexture.Sample(OcclusionSource_s, float2(0,0)).x * LumaSettings.GameSettings.BloomIntensity; // Intensity based on how occluded the sun was
}