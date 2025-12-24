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
  float4 cb0[28];
}

#define cmp

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.x = t1.SampleLevel(s1_s, v0.xy, 0).x;
  r0.y = 1 + -cb2[2].y;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r0.x = -cb2[2].x / r0.x;
  r1.x = cb0[27].x + -r0.x;
  r1.y = -cb0[27].x + r0.x;
  r0.xy = -cb0[27].yy + r1.xy;
  r0.xy = saturate(cb0[27].zz * r0.xy);
  r0.x = dot(r0.xy, cb0[26].xy);
  r0.y = cmp(r0.x < 0.00499999989);
  r0.z = 1 + -r0.x;
  r0.z = 0.75 * r0.z;
  r1.xyz = t2.Sample(s2_s, v0.xy).xyz;
  r2.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r1.xyz = -r2.xyz + r1.xyz;
  r1.xyz = r0.xxx * r1.xyz + r2.xyz;
  r2.xyz = float3(1,0.0500000007,0.0500000007) + -r1.xyz;
  r2.xyz = r0.zzz * r2.xyz + r1.xyz;
  r0.xzw = float3(0.0500000007,0.0500000007,1) + -r1.xyz;
  r3.xyz = r0.xzw * float3(0.800000012,0.800000012,0.800000012) + r1.xyz;
  r0.x = GetLuminance(r1.xyz); // Luma: fixed from BT.601 coeffs
  r3.w = linear_to_gamma1(r0.x, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r0.xzw = r0.xxx + -r1.xyz;
  r2.w = r3.w;
  r3.xyzw = r0.yyyy ? r3.xyzw : r2.xyzw;
  r4.w = r2.w;
  r2.x = cb0[7].x * r2.w;
  r2.y = cb0[7].x * r2.w + -0.5;
  r2.xy = saturate(r2.xy + r2.xy);
  r5.xyzw = cb0[3].xyzw + -cb0[1].xyzw;
  r5.xyzw = r2.xxxx * r5.xyzw + cb0[1].xyzw;
  r6.xyzw = cb0[5].xyzw + -r5.xyzw;
  r2.xyzw = r2.yyyy * r6.xyzw + r5.xyzw;
  r0.xyz = r2.www * r0.xzw + r1.xyz;
  r1.xyz = r2.xyz * r0.xyz;
  r0.xyz = -r2.xyz * r0.xyz + cb0[9].xyz;
  r4.xyz = cb0[9].www * r0.xyz + r1.xyz;
  r0.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r0.xxxx ? r3.xyzw : r4.xyzw;
}