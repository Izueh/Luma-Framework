#include "Includes/Common.hlsl"

SamplerState VideoLuma_s : register(s0);
SamplerState VideoCr_s : register(s1);
SamplerState VideoCb_s : register(s2);
Texture2D<float4> VideoLuma : register(t0);
Texture2D<float4> VideoCr : register(t1);
Texture2D<float4> VideoCb : register(t2);

// For some reason there's 2 near identical video shaders
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  float Cr = VideoCr.Sample(VideoCr_s, v1.xy).x;
  float Y = VideoLuma.Sample(VideoLuma_s, v1.xy).x;
  float Cb = VideoCb.Sample(VideoCb_s, v1.xy).x;

#if DEVELOPMENT && 0 // Test videos // TODO: review colors, and add AutoHDR, maybe emulate the constrast boost from accidentally interepreting them as limited. In both shaders!
  o0.xyz = YUVtoRGB(Y, Cr, Cb, DVS1 * 4);
  if (DVS2)
    o0.xyz = Cr * float3(1.59500003,-0.813000023,0) + Y * float3(1.16400003,1.16400003,1.16400003) + Cb * float3(0,-0.391000003,2.01699996) + float3(-0.870000005,0.528999984,-1.08159995);
#elif 1 // Luma: fixed color space (it was using limited BT.601 but it was full BT.709)
  o0.xyz = YUVtoRGB(Y, Cr, Cb, 0);
#else
  o0.xyz = Cr * float3(1.59500003,-0.813000023,0) + Y * float3(1.16400003,1.16400003,1.16400003) + Cb * float3(0,-0.391000003,2.01699996) + float3(-0.870000005,0.528999984,-1.08159995);
#endif

  o0.w = 1;
}