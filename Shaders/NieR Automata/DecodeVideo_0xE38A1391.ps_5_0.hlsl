// ---- Created with 3Dmigoto v1.3.16 on Tue Dec 16 06:35:04 2025

cbuffer HwShaderBuffer : register(b1)
{
  float4x4 g_WorldMatrix : packoffset(c0);
  float4 g_MatrialColor : packoffset(c4);
  float4 g_Gamma : packoffset(c5);
}

SamplerState g_Texture0Sampler_s : register(s0);
SamplerState g_Texture1Sampler_s : register(s1);
SamplerState g_Texture2Sampler_s : register(s2);
Texture2D<float4> g_Texture0 : register(t0);
Texture2D<float4> g_Texture1 : register(t1);
Texture2D<float4> g_Texture2 : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 1;
  r0.x = r0.x * v2.w + -0.00392156886;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = g_Texture2.Sample(g_Texture2Sampler_s, v1.xy).w;
  r0.x = -0.5 + r0.x;
  r0.xy = float2(1.59599996,0.813000023) * r0.xx;
  r0.z = g_Texture1.Sample(g_Texture1Sampler_s, v1.xy).w;
  r0.z = -0.5 + r0.z;
  r0.y = r0.z * -0.39199999 + -r0.y;
  r0.z = 2.01699996 * r0.z;
  r0.w = g_Texture0.Sample(g_Texture0Sampler_s, v1.xy).w;
  r0.w = -0.0625 + r0.w;
  r1.y = r0.w * 1.16400003 + r0.y;
  r1.x = r0.w * 1.16400003 + r0.x;
  r1.z = r0.w * 1.16400003 + r0.z;
  r0.xyz = v2.xyz * r1.xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = g_Gamma.xxx * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  o0.xyz = r0.xyz;
  o0.w = v2.w;
  r1.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r0.xyz;
  r1.xyz = float3(0.947867334,0.947867334,0.947867334) * r1.xyz;
  r1.xyz = log2(r1.xyz);
  r1.xyz = float3(2.4000001,2.4000001,2.4000001) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r2.xyz = cmp(float3(0.0392800011,0.0392800011,0.0392800011) >= r0.xyz);
  r0.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
  o1.xyz = r2.xyz ? r0.xyz : r1.xyz;
  o1.w = v2.w;
  return;
}