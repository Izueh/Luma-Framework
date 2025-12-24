#include "../Includes/Common.hlsl"

#ifndef ENABLE_HDR_COLOR_GRADING
#define ENABLE_HDR_COLOR_GRADING 1
#endif

cbuffer ColorCorrectionConstants : register(b3)
{
  float4 g_cbColorCorrectionMatrix[3] : packoffset(c0);
  uint2 g_cbTexSize : packoffset(c3);
  uint2 padding : packoffset(c3.z);
}

SamplerState ss_s : register(s0);
Texture2D<float4> inputTex : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyz = inputTex.Sample(ss_s, v2.xy).xyz;
#if 1 // Luma: optionally disable clamping
  r0.yzw = max(float3(0,0,0), r0.xzy);
#else
  r0.yzw = r0.xzy;
#endif
#if ENABLE_HDR_COLOR_GRADING
  r0.x = dot(r0.ywz, g_cbColorCorrectionMatrix[0].xyz);
  r0.y = dot(r0.xwz, g_cbColorCorrectionMatrix[1].xyz);
  o0.z = dot(r0.xyz, g_cbColorCorrectionMatrix[2].xyz);
  o0.xy = r0.xy;
#else
  o0.xzy = r0.yzw;
#endif // ENABLE_HDR_COLOR_GRADING
  o0.w = 1;
}