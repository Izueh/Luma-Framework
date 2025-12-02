#include "Includes/Common.hlsl"

SamplerState SamplerLinear_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyz;
  r0.x = linear_to_gamma1(dot(r0.xyz, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE); // Luma: fix SMAA edges being calculated in linear space instead of gamma, this proves better edge detection and thus better SMAA resolve
  r0.yzw = SceneTexture.SampleLevel(SamplerLinear_s, v2.xy, 0).xyz;
  r1.x = linear_to_gamma1(dot(r0.yzw, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r0.yzw = SceneTexture.SampleLevel(SamplerLinear_s, v2.zw, 0).xyz;
  r1.y = linear_to_gamma1(dot(r0.yzw, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r0.yz = -r1.xy + r0.x;
  r1.zw = (abs(r0.yz) >= 0.05);
  r1.zw = asfloat(asint(r1.zw) & 0x3F800000);
  r0.w = dot(r1.zw, float2(1,1));
  r0.w = (r0.w == 0.0);
  if (r0.w != 0) discard;
  r2.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v3.xy, 0).xyz;
  r2.x = linear_to_gamma1(dot(r2.xyz, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r3.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v3.zw, 0).xyz;
  r2.y = linear_to_gamma1(dot(r3.xyz, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r0.xw = -r2.xy + r0.xx;
  r0.xw = max(abs(r0.yz), abs(r0.xw));
  r0.x = max(r0.x, r0.w);
  r2.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v4.xy, 0).xyz;
  r2.x = linear_to_gamma1(dot(r2.xyz, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r3.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v4.zw, 0).xyz;
  r2.y = linear_to_gamma1(dot(r3.xyz, float3(0.212500006,0.715399981,0.0720999986)), GCT_POSITIVE);
  r1.xy = -r2.xy + r1.xy;
  r0.xw = max(abs(r1.xy), r0.xx);
  r0.xw = float2(0.5,0.5) * r0.xw;
  r0.xy = (abs(r0.yz) >= r0.xw);
  r0.xy = asfloat(asint(r0.xy) & 0x3F800000);
  o0.xy = r1.zw * r0.xy;
  o0.zw = float2(0,0);

  // TODO: add the depth buffer to aid the edges detection. Might be more detrimental than not...
}