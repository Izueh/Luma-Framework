#include "Includes/Common.hlsl"

cbuffer cbConstants : register(b1)
{
  float4 Constants : packoffset(c0);
}

SamplerState Texture0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xy = Constants.xy + v1.xy;
  o0.xyzw = Texture0.Sample(Texture0_s, r0.xy).xyzw;
  
#if 1 // Luma: fix gamma not matching on UI due to Vanilla swapping between sRGB and non sRGB views (we can't in HDR)
  o0.xyz = linear_to_sRGB_gamma(o0.xyz, GCT_MIRROR);
#endif
}