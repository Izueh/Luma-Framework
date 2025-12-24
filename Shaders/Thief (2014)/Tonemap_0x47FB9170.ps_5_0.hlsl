#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // Bloom

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb0 : register(b0)
{
  float4 cb0[27];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  float4 sceneColor = t0.Sample(s0_s, v0.xy).xyzw;
  float someCoeff2 = saturate(sceneColor.a * 2);
  float someCoeff = (someCoeff2 * cb0[26].x) * 0.75;
  someCoeff2 = (1 - someCoeff2) * cb0[26].x;

  float3 bloomColor = t1.Sample(s1_s, v0.xy).xyz;
  bloomColor -= sceneColor.rgb;
  sceneColor.rgb += someCoeff2 * bloomColor;
  bool someBool = someCoeff2 < 0.005;
  r1.xyz = float3(1, 0.05, 0.05) - sceneColor.rgb;
  r1.xyz = someCoeff * r1.xyz + sceneColor.rgb;
  r2.xyz = float3(0.05, 0.05, 1) - sceneColor.rgb;
  r2.xyz = r2.xyz * 0.8 + sceneColor.rgb;
  float lum = GetLuminance(sceneColor.rgb); // Luma: fixed from BT.601 coeffs
  r2.w = linear_to_gamma1(lum, GCT_POSITIVE); // Luma: fixed using sqrt as approximation of gamma 2.2
  r3.xyz = lum - sceneColor.rgb;
  r1.w = r2.w;
  r2.xyzw = someBool ? r2.xyzw : r1.xyzw;

  r4.w = r1.w;
  r1.x = cb0[7].x * r1.w;
  r1.y = cb0[7].x * r1.w - 0.5;
  r1.xy = saturate(r1.xy + r1.xy);
  r5.xyzw = cb0[3].xyzw - cb0[1].xyzw;
  r5.xyzw = r1.x * r5.xyzw + cb0[1].xyzw;
  r6.xyzw = cb0[5].xyzw - r5.xyzw;
  r1.xyzw = r1.y * r6.xyzw + r5.xyzw;
  r0.xyz = r1.w * r3.xyz + sceneColor.xyz;
  r3.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r1.xyz * r0.xyz + cb0[9].xyz;
  r4.xyz = cb0[9].w * r0.xyz + r3.xyz;
  bool some = cb0[25].x == 1.0;
  o0.xyzw = some ? r2.xyzw : r4.xyzw;
}