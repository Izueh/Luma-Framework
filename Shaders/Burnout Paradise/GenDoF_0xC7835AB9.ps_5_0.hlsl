#include "Includes/Common.hlsl"

SamplerState SamplerSource_s : register(s0);
Texture2D<float4> SamplerSourceTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyz = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).xyz;
  r0.xyz = max(r0.xyz, -FLT16_MAX); // Luma: strip away nans
  r0.xyz = IsInfinite_Strict(r0.xyz) ? 1.0 : r0.xyz; // Luma: clamp infinite (we can't have -INF as we previous clip all negative values from materials rendering)
  r1.xyz = SamplerSourceTexture.Sample(SamplerSource_s, v1.zw).xyz;
  r1.xyz = max(r1.xyz, -FLT16_MAX);
  r1.xyz = IsInfinite_Strict(r1.xyz) ? 1.0 : r1.xyz;
  r0.xyz += r1.xyz;
  r1.xyz = SamplerSourceTexture.Sample(SamplerSource_s, v2.xy).xyz;
  r1.xyz = max(r1.xyz, -FLT16_MAX);
  r1.xyz = IsInfinite_Strict(r1.xyz) ? 1.0 : r1.xyz;
  r0.xyz += r1.xyz;
  r1.xyz = SamplerSourceTexture.Sample(SamplerSource_s, v2.zw).xyz;
  r1.xyz = max(r1.xyz, -FLT16_MAX);
  r1.xyz = IsInfinite_Strict(r1.xyz) ? 1.0 : r1.xyz;
  r0.xyz += r1.xyz;
  
  o0.xyz = r0.xyz * 0.25;
  o0.w = 1;
}