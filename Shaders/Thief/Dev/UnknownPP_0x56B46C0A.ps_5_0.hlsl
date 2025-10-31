#include "../../Includes/Common.hlsl"

cbuffer cb0 : register(b0)
{
  float4 cb0[11];
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
  r1.xyz = float3(1.06666672,1.06666672,1.06666672) * r0.xyz;
  r0.w = GetLuminance(r1.xyz); // Luma: fixed from BT.601 coeffs
  r0.xyz = -r0.xyz * float3(1.06666672,1.06666672,1.06666672) + r0.www;
  r0.w = linear_to_gamma1(r0.w, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.x = cb0[7].x * r0.w;
  r2.y = cb0[7].x * r0.w + -0.5;
  r2.xy = saturate(r2.xy + r2.xy);
  r3.xyzw = cb0[3].xyzw + -cb0[1].xyzw;
  r3.xyzw = r2.xxxx * r3.xyzw + cb0[1].xyzw;
  r4.xyzw = cb0[5].xyzw + -r3.xyzw;
  r2.xyzw = r2.yyyy * r4.xyzw + r3.xyzw;
  r0.xyz = r2.www * r0.xyz + r1.xyz;
  r1.xyz = r2.xyz * r0.xyz;
  r0.xyz = -r2.xyz * r0.xyz + cb0[9].xyz;
  o0.xyz = cb0[9].www * r0.xyz + r1.xyz;
  o0.w = 0;
}