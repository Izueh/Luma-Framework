#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Scene

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;

  r0.xyzw = cb0[3].xyzw - cb0[1].xyzw;
  float3 sceneColor = t0.Sample(s0_s, v0.xy).xyz;
  float lum = GetLuminance(sceneColor.rgb); // Luma: fixed from BT.601 coeffs
  r2.x = linear_to_gamma1(lum, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r2.yzw = lum - sceneColor;
  r3.x = cb0[7].x * r2.x;
  r3.y = cb0[7].x * r2.x - 0.5;
  o0.w = r2.x; // Output approximate gamma space luminance for FXAA
  r3.xy = saturate(r3.xy + r3.xy);
  r0.xyzw = r3.x * r0.xyzw + cb0[1].xyzw;
  r4.xyzw = cb0[5].xyzw - r0.xyzw;
  r0.xyzw = r3.y * r4.xyzw + r0.xyzw;
  r1.xyz = r0.w * r2.yzw + sceneColor;
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r0.xyz * r1.xyz + cb0[9].xyz;
  o0.xyz = cb0[9].w * r0.xyz + r2.xyz;
}