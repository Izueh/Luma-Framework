#include "Includes/Common.hlsl"

SamplerState Scene_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float o0 : SV_Target0)
{
  float4 r0;
  r0.xyz = SceneTexture.Sample(Scene_s, v1.xy).xyz;
#if 1 // Luma correct luminance being calculated as BT.601 and in gamma space, and with green and blue swapped
  o0.x = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_MIRROR)), GCT_POSITIVE); // This will clip out nans as well
#else
  o0.x = dot(r0.xzy, float3(0.298999995,0.587000012,0.114));
  o0.x = max(o0.x, 0.0001);
#endif
}