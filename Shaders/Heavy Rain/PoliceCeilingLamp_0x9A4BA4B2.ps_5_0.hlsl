#include "../Includes/Common.hlsl"

cbuffer ConstantBuffer : register(b0)
{
  float4 register0 : packoffset(c0);
  float4 register1 : packoffset(c1);
  float3 register2 : packoffset(c2);
  float4x4 register7 : packoffset(c3);
  float4x4 INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX : packoffset(c7);
  float4 ALPHA_TEST_PARAM : packoffset(c11);
  float4 TEXEL_MARGIN : packoffset(c12);
}

SamplerState SAMPLER_qdAmbientCubemapSampler_s : register(s0);
TextureCube<float4> texture0 : register(t0);

void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD4,
  float4 v2 : COLOR1,
  float3 v3 : TEXCOORD5,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD8,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2)
{
  float4 r0,r1,r2;
  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.w = saturate(1000 * v1.w);
  o0.w = r0.w * 0.699999988 + 0.300000012;
  r1.xyz = v0.xyz * v0.xyz;
  r1.xyz = float3(4,4,4) * r1.xyz;
  r0.w = (register1.w < 0);
  if (r0.w != 0) {
    r0.w = (int)register1.x;
    r2.xyz = register2.xyz + -v1.xyz;
    r2.xyz = float3(0.00999999978,0.00999999978,0.00999999978) * r2.xyz;
    r1.w = dot(r2.xyz, r2.xyz);
    r1.w = rsqrt(r1.w);
    r2.xyz = r2.xyz * r1.www;
    r1.w = dot(r0.xyz, r2.xyz);
    r1.w = r1.w + r1.w;
    r2.xyz = r1.www * r0.xyz + -r2.xyz;
    r2.xyz = r0.www ? r2.xyz : r0.xyz;
    r2.w = -r2.y;
    r2.xyz = texture0.Sample(SAMPLER_qdAmbientCubemapSampler_s, r2.xwz).xyz;
  } else {
    r2.xyz = float3(0,0,0);
  }
  r1.xyz = register0.www * r1.xyz;
  r1.xyz = register0.xyz * v0.xyz + r1.xyz;
  r1.xyz = r1.xyz + r2.xyz;
  r1.xyz = -v2.xyz + r1.xyz;
  o0.xyz = v2.www * r1.xyz + v2.xyz;
  o0.xyz = RestoreLuminance(saturate(o0.xyz), o0.xyz, true); // Luma: fix police office lights having blue tint! // TODO: review, make sure it's not shared. Also maybe make it yellow
  o2.x = v4.z / v4.w;
  o2.yzw = 0;
  r1.x = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m00_m10_m20);
  r1.y = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m01_m11_m21);
  r1.z = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m02_m12_m22);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx + float3(1,1,1);
  o1.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o1.w = 1;

  // Luma
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a); // Note: not clamping this on rgb somehow makes bloom stronger, even if the scene source value used to generate bloom stays within SDR range either way
}