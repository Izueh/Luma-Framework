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
  r0.y = cmp(cb0[24].z >= 0);
  r1.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.z = saturate(r1.w + r1.w);
  r0.z = 1 + -r0.z;
  r0.w = -r0.z * cb0[26].x + 1;
  r0.z = cb0[26].x * r0.z;
  r1.w = cb0[24].z * r0.w;
  r0.w = 0.75 * r0.w;
  r2.x = -cb0[24].z * r0.z;
  r0.y = r0.y ? r1.w : r2.x;
  r0.y = r0.y + -r0.x;
  r0.x = abs(cb0[24].z) * r0.y + r0.x;
  r2.xyzw = cb0[6].xyzw + -cb0[5].xyzw;
  r2.xyzw = r0.xxxx * r2.xyzw + cb0[5].xyzw;
  r3.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r3.xyzw = r0.xxxx * r3.xyzw + cb0[3].xyzw;
  r4.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r4.xyzw = r0.xxxx * r4.xyzw + cb0[1].xyzw;
  r3.xyzw = -r4.xyzw + r3.xyzw;
  r0.y = cb0[8].x + -cb0[7].x;
  r0.x = r0.x * r0.y + cb0[7].x;
  r5.xyz = t2.Sample(s2_s, v0.xy).xyz;
  r5.xyz = r5.xyz + -r1.xyz;
  r1.xyz = r0.zzz * r5.xyz + r1.xyz;
  r0.y = cmp(r0.z < 0.00499999989);
  r0.z = GetLuminance(r1.xyz); // Luma: fixed from BT.601 coeffs
  r5.w = linear_to_gamma1(r0.z, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r6.xyz = r0.zzz + -r1.xyz;
  r7.x = r5.w * r0.x;
  r7.y = r0.x * r5.w + -0.5;
  r0.xz = saturate(r7.xy + r7.xy);
  r3.xyzw = r0.xxxx * r3.xyzw + r4.xyzw;
  r2.xyzw = -r3.xyzw + r2.xyzw;
  r2.xyzw = r0.zzzz * r2.xyzw + r3.xyzw;
  r3.xyz = r2.www * r6.xyz + r1.xyz;
  r4.xyz = r3.xyz * r2.xyz;
  r2.xyz = -r2.xyz * r3.xyz + cb0[9].xyz;
  r2.xyz = cb0[9].www * r2.xyz + r4.xyz;
  r3.xyz = float3(1,0.0500000007,0.0500000007) + -r1.xyz;
  r3.xyz = r0.www * r3.xyz + r1.xyz;
  r0.xzw = float3(0.0500000007,0.0500000007,1) + -r1.xyz;
  r5.xyz = r0.xzw * float3(0.800000012,0.800000012,0.800000012) + r1.xyz;
  r3.w = r5.w;
  r0.xyzw = r0.yyyy ? r5.xyzw : r3.xyzw;
  r2.w = r3.w;
  r1.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r1.xxxx ? r0.xyzw : r2.xyzw;
}