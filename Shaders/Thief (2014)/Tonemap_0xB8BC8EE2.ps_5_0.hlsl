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
  r0.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.w = saturate(r0.w + r0.w);
  r0.w = 1 + -r0.w;
  r1.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r1.w = cb0[26].y * r1.w;
  r2.x = r0.w * cb0[26].x + r1.w;
  r0.w = cb0[26].x * r0.w;
  r0.xyz = r0.www * -r0.xyz + r0.xyz;
  r0.w = 1 + -r2.x;
  r2.x = cmp(r2.x < 0.00499999989);
  r0.w = 0.75 * r0.w;
  r1.xyz = r1.xyz + -r0.xyz;
  r0.xyz = r1.www * r1.xyz + r0.xyz;
  r1.xyz = float3(1,0.0500000007,0.0500000007) + -r0.xyz;
  r1.xyz = r0.www * r1.xyz + r0.xyz;
  r2.yzw = float3(0.0500000007,0.0500000007,1) + -r0.xyz;
  r3.xyz = r2.yzw * float3(0.800000012,0.800000012,0.800000012) + r0.xyz;
  r0.w = GetLuminance(r0.xyz); // Luma: fixed from BT.601 coeffs
  r3.w = linear_to_gamma1(r0.w, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.yzw = r0.www + -r0.xyz;
  r1.w = r3.w;
  r3.xyzw = r2.xxxx ? r3.xyzw : r1.xyzw;
  r4.w = r1.w;
  r1.x = cb0[7].x * r1.w;
  r1.y = cb0[7].x * r1.w + -0.5;
  r1.xy = saturate(r1.xy + r1.xy);
  r5.xyzw = cb0[3].xyzw + -cb0[1].xyzw;
  r5.xyzw = r1.xxxx * r5.xyzw + cb0[1].xyzw;
  r6.xyzw = cb0[5].xyzw + -r5.xyzw;
  r1.xyzw = r1.yyyy * r6.xyzw + r5.xyzw;
  r0.xyz = r1.www * r2.yzw + r0.xyz;
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r1.xyz * r0.xyz + cb0[9].xyz;
  r4.xyz = cb0[9].www * r0.xyz + r2.xyz;
  r0.x = cmp(cb0[25].x == 1.000000);
  o0.xyzw = r0.xxxx ? r3.xyzw : r4.xyzw;
}