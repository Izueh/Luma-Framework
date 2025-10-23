#include "Includes/Common.hlsl"

SamplerState SamplerSource_s : register(s0);
SamplerState SamplerParticles_s : register(s1);
Texture2D<float4> SamplerSourceTexture : register(t0);
Texture2D<float4> SamplerParticlesTexture : register(t1);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = SamplerParticlesTexture.Sample(SamplerParticles_s, v1.xy).xyzw;
  r0.w = 1 - r0.w;
  r1.xyzw = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).xyzw;
  r1.xyzw = IsNaN_Strict(r1.xyzw) ? 0.0 : r1.xyzw; // Luma: nans protection
  o0.xyz = r1.xyz * r0.w + r0.xyz;
  o0.w = r1.w;
}