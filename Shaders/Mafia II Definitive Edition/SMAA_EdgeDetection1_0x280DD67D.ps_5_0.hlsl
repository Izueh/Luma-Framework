#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 SMAA_RT_METRICS : packoffset(c1);
  float4 SMAAParams : packoffset(c2);
  float4 SMAAParams2 : packoffset(c3);
}

SamplerState TMU0_sampler_s : register(s0);
SamplerState TMU1_sampler_s : register(s1);
Texture2D<float4> TMU0 : register(t0);
Texture2D<float4> TMU1 : register(t1);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = TMU1.Sample(TMU1_sampler_s, v1.xy).x;
  r0.y = TMU1.Sample(TMU1_sampler_s, v2.xy).x;
  r0.z = TMU1.Sample(TMU1_sampler_s, v2.zw).x;
  r0.xy = r0.xx + -r0.yz;
  r0.xy = cmp(abs(r0.xy) >= SMAAParams2.xx);
  r0.xy = r0.xy ? float2(1,1) : 0;
  r0.z = SMAAParams2.y * SMAAParams.x;
  r0.xy = -SMAAParams2.zz * r0.xy + float2(1,1);
  r0.xy = r0.zz * r0.xy;
  r1.xyz = TMU0.Sample(TMU0_sampler_s, v1.xy).xyz;
  r0.z = linear_to_gamma1(GetLuminance(gamma_to_linear(r1.xyz, GCT_POSITIVE))); // Luma: fixed calculating luminance in gamma space
  r1.xyz = TMU0.Sample(TMU0_sampler_s, v2.xy).xyz;
  r1.x = linear_to_gamma1(GetLuminance(gamma_to_linear(r1.xyz, GCT_POSITIVE)));
  r2.xyz = TMU0.Sample(TMU0_sampler_s, v2.zw).xyz;
  r1.y = linear_to_gamma1(GetLuminance(gamma_to_linear(r2.xyz, GCT_POSITIVE)));
  r1.zw = -r1.xy + r0.zz;
  r0.xy = cmp(abs(r1.zw) >= r0.xy);
  r0.xy = r0.xy ? float2(1,1) : 0;
  r0.w = dot(r0.xy, float2(1,1));
  r0.w = cmp(r0.w == 0.000000);
  if (r0.w != 0) discard;
  r2.xyz = TMU0.Sample(TMU0_sampler_s, v3.xy).xyz;
  r2.x = linear_to_gamma1(GetLuminance(gamma_to_linear(r2.xyz, GCT_POSITIVE)));
  r3.xyz = TMU0.Sample(TMU0_sampler_s, v3.zw).xyz;
  r2.y = linear_to_gamma1(GetLuminance(gamma_to_linear(r3.xyz, GCT_POSITIVE)));
  r0.zw = -r2.xy + r0.zz;
  r0.zw = max(abs(r1.zw), abs(r0.zw));
  r2.xyz = TMU0.Sample(TMU0_sampler_s, v4.xy).xyz;
  r2.x = linear_to_gamma1(GetLuminance(gamma_to_linear(r2.xyz, GCT_POSITIVE)));
  r3.xyz = TMU0.Sample(TMU0_sampler_s, v4.zw).xyz;
  r2.y = linear_to_gamma1(GetLuminance(gamma_to_linear(r3.xyz, GCT_POSITIVE)));
  r1.xy = -r2.xy + r1.xy;
  r0.zw = max(abs(r1.xy), r0.zw);
  r0.z = max(r0.z, r0.w);
  r1.xy = abs(r1.zw) + abs(r1.zw);
  r0.zw = cmp(r1.xy >= r0.zz);
  r0.zw = r0.zw ? float2(1,1) : 0;
  r0.xy = r0.xy * r0.zw;
  o0.xy = r0.xy;
  o0.zw = float2(0,0);
}