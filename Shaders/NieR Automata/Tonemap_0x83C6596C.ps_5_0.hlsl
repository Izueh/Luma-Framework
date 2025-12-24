cbuffer PSParamBuffer1 : register(b1)
{
  float4 g_ExposureParam : packoffset(c0);
  float4 g_Brightness : packoffset(c1);
  float4 g_VignettoParam : packoffset(c2);
}

SamplerState g_Texture0Sampler_s : register(s0);
SamplerState g_Texture1Sampler_s : register(s1);
SamplerState g_Texture2Sampler_s : register(s2);
SamplerState g_Texture3Sampler_s : register(s3);
Texture2D<float4> g_Texture0 : register(t0);
Texture2D<float4> g_Texture1 : register(t1);
Texture2D<float4> g_Texture2 : register(t2);
Texture2D<float4> g_Texture3 : register(t3);
StructuredBuffer<float> g_AverageBrightnessBuffer : register(t5);

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3;
  r0.xy = float2(-0.5,-0.5) + v1.xy;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = 1 + -r0.x;
  r0.x = saturate(r0.x / g_VignettoParam.x);
  r0.x = log2(r0.x);
  r0.x = g_VignettoParam.y * r0.x;
  r0.x = exp2(r0.x);
  r0.yzw = g_Texture2.Sample(g_Texture2Sampler_s, v1.xy).xyz;
  r1.xyz = g_Texture0.Sample(g_Texture0Sampler_s, v1.xy).xyz;
  r0.xyz = r1.xyz * r0.xxx + r0.yzw;
  r0.w = g_AverageBrightnessBuffer[0].x;
  r1.xyz = r0.xyz * r0.www;
  r2.xyz = r1.xyz * float3(0.666666687,0.666666687,0.666666687) + float3(1,1,1);
  r3.xyz = r2.xyz * r1.xyz;
  r1.xyz = r1.xyz * r2.xyz + float3(1,1,1);
  r1.xyz = r3.xyz / r1.xyz;
  r0.w = cmp(0 < g_ExposureParam.y);
  r1.xyz = r0.www ? r1.xyz : r0.xyz;
  r0.xyz = -r1.xyz + r0.xyz;
  r0.xyz = r0.xyz * float3(0.200000003,0.200000003,0.200000003) + r1.xyz;
  r1.xyzw = saturate(r1.zzxy);
  r2.xyw = float3(14.9998999,0.9375,0.05859375) * r1.xwz;
  r0.w = floor(r2.x);
  r2.x = r0.w * 0.0625 + r2.w;
  r2.xyzw = float4(0.001953125,0.03125,0.064453125,0.03125) + r2.xyxy;
  r0.w = r1.y * 15 + -r0.w;
  r1.xyz = g_Texture1.Sample(g_Texture1Sampler_s, r2.zw).xyz;
  r2.xyz = g_Texture1.Sample(g_Texture1Sampler_s, r2.xy).xyz;
  r1.xyz = -r2.xyz + r1.xyz;
  o0.xyz = r0.www * r1.xyz + r2.xyz;
  o0.w = 0;
  r1.xyzw = saturate(r0.zzxy);
  r2.xyw = float3(14.9998999,0.9375,0.05859375) * r1.xwz;
  r0.w = floor(r2.x);
  r2.x = r0.w * 0.0625 + r2.w;
  r2.xyzw = float4(0.001953125,0.03125,0.064453125,0.03125) + r2.xyxy;
  r0.w = r1.y * 15 + -r0.w;
  r1.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r2.zw).xyz;
  r2.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r2.xy).xyz;
  r1.xyz = -r2.xyz + r1.xyz;
  r1.xyz = r0.www * r1.xyz + r2.xyz;
  r2.xyz = -r1.xyz + r0.xyz;
  r0.x = dot(r0.xyz, float3(0.298911989,0.586610973,0.114478));
  r0.x = -0.25 + r0.x;
  r0.x = saturate(1.33333337 * r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  o1.xyz = r0.xxx * r2.xyz + r1.xyz;
  o1.w = 0;
}