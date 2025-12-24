#include "../Includes/Common.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

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
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xyzw = cb0[4].xyzw + -cb0[3].xyzw;
  r1.x = cmp(cb0[24].z >= 0);
  r2.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r1.y = saturate(r2.w + r2.w);
  r1.y = 1 + -r1.y;
  r1.z = -r1.y * cb0[26].x + 1;
  r1.y = cb0[26].x * r1.y;
  r1.w = cb0[24].z * r1.z;
  r1.z = 0.75 * r1.z;
  r2.w = -cb0[24].z * r1.y;
  r1.x = r1.x ? r1.w : r2.w;
  r0.xyzw = r1.xxxx * r0.xyzw + cb0[3].xyzw;
  r3.xyzw = cb0[2].xyzw + -cb0[1].xyzw;
  r3.xyzw = r1.xxxx * r3.xyzw + cb0[1].xyzw;
  r0.xyzw = -r3.xyzw + r0.xyzw;
  r1.w = cb0[8].x + -cb0[7].x;
  r1.w = r1.x * r1.w + cb0[7].x;
  r4.xyz = t1.Sample(s1_s, v0.xy).xyz;
  r4.xyz = r4.xyz + -r2.xyz;
  r2.xyz = r1.yyy * r4.xyz + r2.xyz;
  r1.y = cmp(r1.y < 0.00499999989);
  r2.w = GetLuminance(r2.xyz); // Luma: fixed from BT.601 coeffs
  r4.w = linear_to_gamma1(r2.w, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r5.xyz = r2.www + -r2.xyz;
  r6.x = r4.w * r1.w;
  r6.y = r1.w * r4.w + -0.5;
  r6.xy = saturate(r6.xy + r6.xy);
  r0.xyzw = r6.xxxx * r0.xyzw + r3.xyzw;
  r3.xyzw = cb0[6].xyzw + -cb0[5].xyzw;
  r3.xyzw = r1.xxxx * r3.xyzw + cb0[5].xyzw;
  r3.xyzw = r3.xyzw + -r0.xyzw;
  r0.xyzw = r6.yyyy * r3.xyzw + r0.xyzw;
  r3.xyz = r0.www * r5.xyz + r2.xyz;
  r5.xyz = r3.xyz * r0.xyz;
  r0.xyz = -r0.xyz * r3.xyz + cb0[9].xyz;
  r0.xyz = cb0[9].www * r0.xyz + r5.xyz;
  r3.xyz = float3(1,0.0500000007,0.0500000007) + -r2.xyz;
  r3.xyz = r1.zzz * r3.xyz + r2.xyz;
  r1.xzw = float3(0.0500000007,0.0500000007,1) + -r2.xyz;
  r4.xyz = r1.xzw * float3(0.800000012,0.800000012,0.800000012) + r2.xyz;
  r3.w = r4.w;
  r1.xyzw = r1.yyyy ? r4.xyzw : r3.xyzw;
  r0.w = r3.w;
  r2.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r2.xxxx ? r1.xyzw : r0.xyzw;
}