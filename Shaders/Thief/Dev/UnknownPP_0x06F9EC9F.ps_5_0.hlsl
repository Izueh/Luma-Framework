#include "../../Includes/Common.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[13];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xz = float2(-0.001953125,-0.03125) + v0.xy;
  r1.x = 16 * r0.x;
  r0.w = frac(r1.x);
  r0.y = -r0.w * 0.0625 + r0.x;
  r0.xyz = cb0[10].xxx * r0.yzw;
  r0.xyz = float3(1.06666672,1.06666672,1.06666672) * r0.xyz;
  r0.w = 0;
  r1.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.xyzw = cb0[11].xxxx * r1.xyzw + r0.xyzw;
  r1.xyzw = t1.Sample(s1_s, v0.xy).xyzw;
  r0.xyzw = cb0[12].xxxx * r1.xyzw + r0.xyzw;
  r1.x = GetLuminance(r0.xyz); // Luma: fixed from BT.601 coeffs
  r1.yzw = r1.xxx + -r0.xyz;
  r1.x = linear_to_gamma1(r1.x, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.x = cb0[7].x * r1.x;
  r2.y = cb0[7].x * r1.x + -0.5;
  r2.xy = saturate(r2.xy + r2.xy);
  r3.xyzw = cb0[3].xyzw + -cb0[1].xyzw;
  r3.xyzw = r2.xxxx * r3.xyzw + cb0[1].xyzw;
  r4.xyzw = cb0[5].xyzw + -r3.xyzw;
  r2.xyzw = r2.yyyy * r4.xyzw + r3.xyzw;
  r0.xyz = r2.www * r1.yzw + r0.xyz;
  o0.w = r0.w;
  r1.xyz = r2.xyz * r0.xyz;
  r0.xyz = -r2.xyz * r0.xyz + cb0[9].xyz;
  o0.xyz = cb0[9].www * r0.xyz + r1.xyz;
}