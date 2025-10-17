#include "../Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

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
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = cb0[8].x + -cb0[7].x;
  r0.y = cmp(cb0[24].z >= 0);
  r1.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.z = saturate(r1.w + r1.w);
  r0.z = 1 + -r0.z;
  r2.xyzw = t2.Sample(s2_s, v0.xy).xyzw;
  r0.w = cb0[26].y * r2.w;
  r1.w = r0.z * cb0[26].x + r0.w;
  r0.z = cb0[26].x * r0.z;
  r2.w = 1 + -r1.w;
  r3.x = cb0[24].z * r2.w;
  r2.w = 0.75 * r2.w;
  r3.y = -cb0[24].z * r1.w;
  r1.w = cmp(r1.w < 0.00499999989);
  r0.y = r0.y ? r3.x : r3.y;
  r0.x = r0.y * r0.x + cb0[7].x;
  r3.xyz = t1.Sample(s1_s, v0.xy).xyz;
  r3.xyz = r3.xyz + -r1.xyz;
  r1.xyz = r0.zzz * r3.xyz + r1.xyz;
  r2.xyz = r2.xyz + -r1.xyz;
  r1.xyz = r0.www * r2.xyz + r1.xyz;
  r0.z = GetLuminance(r1.xyz); // Luma: fixed from BT.601 coeffs
  r3.w = linear_to_gamma1(r0.z, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.xyz = r0.zzz + -r1.xyz;
  r4.x = r3.w * r0.x;
  r4.y = r0.x * r3.w + -0.5;
  r0.xz = saturate(r4.xy + r4.xy);
  r4.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r4.xyzw = r0.yyyy * r4.xyzw + cb0[3].xyzw;
  r5.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r5.xyzw = r0.yyyy * r5.xyzw + cb0[1].xyzw;
  r4.xyzw = -r5.xyzw + r4.xyzw;
  r4.xyzw = r0.xxxx * r4.xyzw + r5.xyzw;
  r5.xyzw = cb0[6].xyzw + -cb0[5].xyzw;
  r5.xyzw = r0.yyyy * r5.xyzw + cb0[5].xyzw;
  r5.xyzw = r5.xyzw + -r4.xyzw;
  r0.xyzw = r0.zzzz * r5.xyzw + r4.xyzw;
  r2.xyz = r0.www * r2.xyz + r1.xyz;
  r4.xyz = r2.xyz * r0.xyz;
  r0.xyz = -r0.xyz * r2.xyz + cb0[9].xyz;
  r0.xyz = cb0[9].www * r0.xyz + r4.xyz;
  r2.xyz = float3(1,0.0500000007,0.0500000007) + -r1.xyz;
  r2.xyz = r2.www * r2.xyz + r1.xyz;
  r4.xyz = float3(0.0500000007,0.0500000007,1) + -r1.xyz;
  r3.xyz = r4.xyz * float3(0.800000012,0.800000012,0.800000012) + r1.xyz;
  r2.w = r3.w;
  r1.xyzw = r1.wwww ? r3.xyzw : r2.xyzw;
  r0.w = r2.w;
  r2.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
}