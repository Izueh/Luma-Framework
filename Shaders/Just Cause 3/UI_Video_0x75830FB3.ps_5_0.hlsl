#include "Includes/Common.hlsl"

SamplerState sampler_tex_0__s : register(s0);
SamplerState sampler_tex_1__s : register(s1);
SamplerState sampler_tex_2__s : register(s2);
Texture2D<float> tex_0_ : register(t0);
Texture2D<float> tex_1_ : register(t1);
Texture2D<float> tex_2_ : register(t2);

// For some reason there's 2 near identical video shaders
void main(
  float4 v0 : COLOR0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.x = tex_2_.Sample(sampler_tex_2__s, v1.xy).x;
  r0.x = -0.501960814 + r0.x;
  r0.xyz = float3(1.59599996,-0.813000023,0) * r0.xxx;
  r0.w = tex_0_.Sample(sampler_tex_0__s, v1.xy).x;
  r0.w = -0.0627451017 + r0.w;
  r0.xyz = r0.w * float3(1.16400003,1.16400003,1.16400003) + r0.xyz;
  r0.w = tex_1_.Sample(sampler_tex_1__s, v1.xy).x;
  r0.w = -0.501960814 + r0.w;
  o0.xyz = r0.w * float3(0,-0.39199999,2.01699996) + r0.xyz;
  o0.w = v0.w;
  
#if 1 // Luma: fixed color space (it was using limited BT.601 but it was full BT.709)
  float Y = tex_0_.Sample(sampler_tex_0__s, v1.xy).x;
  float Cr = tex_2_.Sample(sampler_tex_2__s, v1.xy).x;
  float Cb = tex_1_.Sample(sampler_tex_1__s, v1.xy).x;

  o0.rgb = YUVtoRGB(Y, Cr, Cb, 0);
#endif
}