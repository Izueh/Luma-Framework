#include "../Includes/Common.hlsl"

Texture3D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[33];
}

void main(
  linear noperspective float2 v0 : TEXCOORD0,
  linear noperspective float3 v1 : TEXCOORD1,
  linear noperspective float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = -cb1[1].xy + v5.xy;
  r0.xy = cb1[2].zw * r0.xy;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = r0.xyz * cb2[0].xyz + cb0[31].xyz;
  r1.xyz = t2.Sample(s2_s, v0.xy).xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r1.xyz = t1.Sample(s1_s, v0.xy).xyz;
  r0.xyz = r1.xyz * cb0[30].xyz + r0.xyz;
  r0.xyz = v1.xxx * r0.xyz;
  r1.xy = cb0[32].zz * v1.yz;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = 1 + r0.w;
  r0.w = rcp(r0.w);
  r0.w = r0.w * r0.w;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = log2(r0.xyz);
  r0.xyz = saturate(r0.xyz * float3(0.0714285746,0.0714285746,0.0714285746) + float3(0.610726953,0.610726953,0.610726953));
  r0.xyz = r0.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r0.xyz = t3.Sample(s3_s, r0.xyz).xyz;
  r0.w = v2.w * 543.309998 + v2.z;
  r0.w = sin(r0.w);
  r0.w = 493013 * r0.w;
  r0.w = frac(r0.w);
  r0.w = r0.w * 0.00390625 + -0.001953125;
  o0.xyz = r0.xyz * float3(1.04999995,1.04999995,1.04999995) + r0.www;
  r0.xyz = float3(1.04999995,1.04999995,1.04999995) * r0.xyz;
  o0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114));
}