#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // ?

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb0 : register(b0)
{
  float4 cb0[25];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = t1.SampleLevel(s1_s, v0.xy, 0).x;
  r0.y = 1 + -cb2[2].y;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r0.x = -cb2[2].x / r0.x;
  r0.xyz = v1.xyz * r0.xxx;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = -cb0[24].x + r0.x;
  r0.x = saturate(cb0[24].y * r0.x);
  r1.xyzw = cb0[6].xyzw + -cb0[5].xyzw;
  r1.xyzw = r0.x * r1.xyzw + cb0[5].xyzw;
  r2.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r2.xyzw = r0.x * r2.xyzw + cb0[3].xyzw;
  r3.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r3.xyzw = r0.x * r3.xyzw + cb0[1].xyzw;
  r2.xyzw = -r3.xyzw + r2.xyzw;
  r0.y = cb0[8].x + -cb0[7].x;
  r0.x = r0.x * r0.y + cb0[7].x;
  r0.yzw = t0.Sample(s0_s, v0.xy).xyz;
  float lum = GetLuminance(r0.yzw); // Luma: fixed from BT.601 coeffs
  r4.y = linear_to_gamma1(lum, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r4.xzw = lum + -r0.yzw;
  r5.x = r4.y * r0.x;
  r5.y = r0.x * r4.y + -0.5;
  o0.w = r4.y;
  r5.xy = saturate(r5.xy + r5.xy);
  r2.xyzw = r5.x * r2.xyzw + r3.xyzw;
  r1.xyzw = -r2.xyzw + r1.xyzw;
  r1.xyzw = r5.y * r1.xyzw + r2.xyzw;
  r0.xyz = r1.w * r4.xzw + r0.yzw;
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r1.xyz * r0.xyz + cb0[9].xyz;
  o0.xyz = cb0[9].w * r0.xyz + r2.xyz;
}