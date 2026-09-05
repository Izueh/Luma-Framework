#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;

  r0.xyz = r0.xyz / r0.w;
  r0.xyz = r0.xyz * 0.25;
  r1.xyz = v2.x * r0.xyz;
  r2.xyz = r1.xyz * v2.y + 1.0;
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = r0.xyz * v2.x + 1.0;

  o0.xyz = r1.xyz / r2.xyz;

#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear! Also why is it calculating the non decoded luminance?
  o0.w = GetLuminance(r0.xyz);
#else
  o0.w = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
#endif
}