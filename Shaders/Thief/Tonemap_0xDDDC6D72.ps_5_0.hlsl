#include "../Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[27];
}

#define cmp

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
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
  r1.xyzw = r0.xxxx * r1.xyzw + cb0[5].xyzw;
  r2.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r2.xyzw = r0.xxxx * r2.xyzw + cb0[3].xyzw;
  r3.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r3.xyzw = r0.xxxx * r3.xyzw + cb0[1].xyzw;
  r2.xyzw = -r3.xyzw + r2.xyzw;
  r0.y = cb0[8].x + -cb0[7].x;
  r0.x = r0.x * r0.y + cb0[7].x;
  r0.yzw = t2.Sample(s2_s, v0.xy).xyz;
  r4.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.yzw = -r4.xyz + r0.yzw;
  r4.w = saturate(r4.w + r4.w);
  r4.w = 1 + -r4.w;
  r5.x = cb0[26].x * r4.w;
  r4.w = -r4.w * cb0[26].x + 1;
  r4.w = 0.75 * r4.w;
  r0.yzw = r5.xxx * r0.yzw + r4.xyz;
  r4.x = cmp(r5.x < 0.00499999989);
  r4.y = GetLuminance(r0.yzw); // Luma: fixed from BT.601 coeffs
  r5.w = linear_to_gamma1(r4.y, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r6.xyz = r4.yyy + -r0.yzw;
  r7.x = r5.w * r0.x;
  r7.y = r0.x * r5.w + -0.5;
  r4.yz = saturate(r7.xy + r7.xy);
  r2.xyzw = r4.yyyy * r2.xyzw + r3.xyzw;
  r1.xyzw = -r2.xyzw + r1.xyzw;
  r1.xyzw = r4.zzzz * r1.xyzw + r2.xyzw;
  r2.xyz = r1.www * r6.xyz + r0.yzw;
  r3.xyz = r2.xyz * r1.xyz;
  r1.xyz = -r1.xyz * r2.xyz + cb0[9].xyz;
  r1.xyz = cb0[9].www * r1.xyz + r3.xyz;
  r2.xyz = float3(1,0.0500000007,0.0500000007) + -r0.yzw;
  r2.xyz = r4.www * r2.xyz + r0.yzw;
  r3.xyz = float3(0.0500000007,0.0500000007,1) + -r0.yzw;
  r5.xyz = r3.xyz * float3(0.800000012,0.800000012,0.800000012) + r0.yzw;
  r2.w = r5.w;
  r0.xyzw = r4.xxxx ? r5.xyzw : r2.xyzw;
  r1.w = r2.w;
  r2.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r2.xxxx ? r0.xyzw : r1.xyzw;
}