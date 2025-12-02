#include "../Includes/Common.hlsl"

Texture2D<float> t3 : register(t3);
Texture2D<float> t2 : register(t2);
Texture2D<float> t1 : register(t1);
Texture2D<float> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : COLOR1,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.x = t2.Sample(s3_s, v1.xy).x;
  r0.x = -0.501960814 + r0.x;
  r0.xyz = float3(1.59599996,-0.813000023,0) * r0.xxx;
  r0.w = t0.Sample(s0_s, v1.xy).x;
  r0.w = -0.0627451017 + r0.w;
  r0.xyz = r0.www * float3(1.16400003,1.16400003,1.16400003) + r0.xyz;
  r0.w = t1.Sample(s1_s, v1.xy).x;
  r0.w = -0.501960814 + r0.w;
  o0.xyz = r0.www * float3(0,-0.39199999,2.01699996) + r0.xyz;

  r0.x = t3.Sample(s2_s, v1.xy).x;
  o0.w = v0.w * r0.x;

  float Y = t0.Sample(s0_s, v1.xy).x;
  float Cr = t2.Sample(s1_s, v1.xy).x;
  float Cb = t1.Sample(s3_s, v1.xy).x;

  // TODO: properly decode videos, and add AutoHDR
  o0.rgb = YUVtoRGB(Y, Cr, Cb, 1);
}