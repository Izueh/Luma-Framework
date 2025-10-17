#include "Includes/Common.hlsl"

cbuffer ConstantBuffer : register(b0)
{
  float4 register0 : packoffset(c0);
  float4 register1 : packoffset(c1);
  float3 register2 : packoffset(c2);
  float4x4 register7 : packoffset(c3);
  float3 register11 : packoffset(c7);
  float3 register12 : packoffset(c8);
  float3 register13 : packoffset(c9);
  float4x4 INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX : packoffset(c10);
  float4 ALPHA_TEST_PARAM : packoffset(c14);
  float4 TEXEL_MARGIN : packoffset(c15);
}

SamplerState SAMPLER_DTRT_ZM_02_ddsSampler_s : register(s0);
SamplerState SAMPLER_CLOTH_LYQ_05_D_ddsSampler_s : register(s1);
SamplerState SAMPLER_AOSampler_s : register(s2);
SamplerState SAMPLER_qdAmbientCubemapSampler_s : register(s3);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);
TextureCube<float4> texture3 : register(t3);
 
void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD4,
  float4 v3 : COLOR1,
  float3 v4 : TEXCOORD5,
  float4 v5 : TEXCOORD2,
  float4 v6 : TEXCOORD3,
  float4 v7 : TEXCOORD8,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2)
{
  float4 r0,r1,r2,r3,r4;
  r0.x = dot(v4.xyz, v4.xyz);
  r0.x = rsqrt(max(r0.x, FLT_EPSILON)); // LUMA: fixed sqrt of <= 0
  r0.xyz = v4.xyz * r0.xxx;
  r0.w = texture2.Sample(SAMPLER_AOSampler_s, v0.zw).x;
  r1.xyz = texture1.Sample(SAMPLER_CLOTH_LYQ_05_D_ddsSampler_s, v0.xy).xyz;
  r1.w = texture0.Sample(SAMPLER_DTRT_ZM_02_ddsSampler_s, v1.xy).x;
  r1.xyz = float3(-0.581358016,-0.586780012,-0.53749001) + r1.xyz;
  r1.xyz = r1.www * r1.xyz + float3(0.581358016,0.586780012,0.53749001);
  r1.w = dot(r0.xyz, register11.xyz);
  r1.w = r1.w * 0.5 + 0.5;
  r2.xyz = register13.xyz + -register12.xyz;
  r2.xyz = r1.www * r2.xyz + register12.xyz;
  if (register1.w < 0.0) {
    r3.xyz = register2.xyz + -v2.xyz;
    r3.xyz = float3(0.00999999978,0.00999999978,0.00999999978) * r3.xyz;
    r2.w = dot(r3.xyz, r3.xyz);
    r2.w = rsqrt(r2.w);
    r3.xyz = r3.xyz * r2.www;
    r2.w = dot(r0.xyz, r3.xyz);
    r2.w = r2.w + r2.w;
    r3.xyz = r2.www * r0.xyz + -r3.xyz;
    r3.xyz = int(register1.x) ? r3.xyz : r0.xyz;
    r3.w = -r3.y;
    r3.xyz = texture3.Sample(SAMPLER_qdAmbientCubemapSampler_s, r3.xwz).xyz;
  } else {
    r3.xyz = float3(0,0,0);
  }
  r4.xyz = register0.xyz * r0.www;
  r2.xyz = r2.xyz * r1.xyz;
  r2.xyz = r2.xyz * r0.www;
  r1.xyz = r4.xyz * r1.xyz + r2.xyz;
  r1.xyz = r1.xyz + r3.xyz;
  r1.xyz = -v3.xyz + r1.xyz;
  o0.xyz = v3.www * r1.xyz + v3.xyz;
  o2.x = v5.z / v5.w;
  o2.yzw = 0.0;
  r1.x = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m00_m10_m20);
  r1.y = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m01_m11_m21);
  r1.z = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m02_m12_m22);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx + float3(1,1,1);
  o1.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o0.w = 0;
  o1.w = 1;
}