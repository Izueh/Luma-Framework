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
  r0.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r1.x = t1.SampleLevel(s1_s, v0.xy, 0).x;
  r1.y = 1 + -cb2[2].y;
  r1.x = r1.x + -r1.y;
  r1.x = min(-9.99999996e-013, r1.x);
  r1.x = -cb2[2].x / r1.x;
  r1.yzw = v1.xyz * r1.xxx;
  r1.y = dot(r1.yzw, r1.yzw);
  r1.y = -cb0[24].x + r1.y;
  r1.y = saturate(cb0[24].y * r1.y);
  r0.xyzw = r1.yyyy * r0.xyzw + cb0[3].xyzw;
  r2.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r2.xyzw = r1.yyyy * r2.xyzw + cb0[1].xyzw;
  r0.xyzw = -r2.xyzw + r0.xyzw;
  r1.z = cb0[8].x + -cb0[7].x;
  r1.z = r1.y * r1.z + cb0[7].x;
  r3.x = cb0[27].x + -r1.x;
  r3.y = -cb0[27].x + r1.x;
  r1.xw = -cb0[27].yy + r3.xy;
  r1.xw = saturate(cb0[27].zz * r1.xw);
  r1.x = dot(r1.xw, cb0[26].xy);
  r3.xyz = t2.Sample(s2_s, v0.xy).xyz;
  r4.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r3.xyz = -r4.xyz + r3.xyz;
  r3.xyz = r1.xxx * r3.xyz + r4.xyz;
  r1.w = GetLuminance(r3.xyz); // Luma: fixed from BT.601 coeffs
  r4.w = linear_to_gamma1(r1.w, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r5.xyz = r1.www + -r3.xyz;
  r6.x = r4.w * r1.z;
  r6.y = r1.z * r4.w + -0.5;
  r1.zw = saturate(r6.xy + r6.xy);
  r0.xyzw = r1.zzzz * r0.xyzw + r2.xyzw;
  r2.xyzw = cb0[6].xyzw + -cb0[5].xyzw;
  r2.xyzw = r1.yyyy * r2.xyzw + cb0[5].xyzw;
  r2.xyzw = r2.xyzw + -r0.xyzw;
  r0.xyzw = r1.wwww * r2.xyzw + r0.xyzw;
  r1.yzw = r0.www * r5.xyz + r3.xyz;
  r2.xyz = r1.yzw * r0.xyz;
  r0.xyz = -r0.xyz * r1.yzw + cb0[9].xyz;
  r0.xyz = cb0[9].www * r0.xyz + r2.xyz;
  r1.y = 1 + -r1.x;
  r1.x = cmp(r1.x < 0.00499999989);
  r1.y = 0.75 * r1.y;
  r2.xyz = float3(1,0.0500000007,0.0500000007) + -r3.xyz;
  r2.xyz = r1.yyy * r2.xyz + r3.xyz;
  r1.yzw = float3(0.0500000007,0.0500000007,1) + -r3.xyz;
  r4.xyz = r1.yzw * float3(0.800000012,0.800000012,0.800000012) + r3.xyz;
  r2.w = r4.w;
  r1.xyzw = r1.xxxx ? r4.xyzw : r2.xyzw;
  r0.w = r2.w;
  r2.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
}