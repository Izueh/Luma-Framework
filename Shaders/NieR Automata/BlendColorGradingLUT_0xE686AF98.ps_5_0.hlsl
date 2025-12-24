cbuffer PSParamBuffer1 : register(b1)
{
  float4 g_ExposureParam : packoffset(c0);
  float4 g_Brightness : packoffset(c1);
  float4 g_VignettoParam : packoffset(c2);
}

SamplerState g_Texture1Sampler_s : register(s1);
SamplerState g_Texture3Sampler_s : register(s3);
Texture2D<float4> g_Texture1 : register(t1);
Texture2D<float4> g_Texture3 : register(t3);

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3;
  r0.x = 0.0625 * v0.x;
  r0.y = cmp(r0.x >= -r0.x);
  r0.z = frac(abs(r0.x));
  r1.xy = floor(r0.xx);
  r0.x = r0.y ? r0.z : -r0.z;
  r0.x = 16 * r0.x;
  r1.z = floor(r0.x);
  r1.w = floor(v0.y);
  r0.xyzw = float4(0.0666666701,0.0666666701,0.0666666701,0.0666666701) * r1.yyzw;
  r0.xyzw = log2(abs(r0.xyzw));
  r0.xyzw = float4(0.416666657,0.416666657,0.416666657,0.416666657) * r0.xyzw;
  r0.xyzw = exp2(r0.xyzw);
  r0.xyzw = r0.xyzw * float4(1.05499995,1.05499995,1.05499995,1.05499995) + float4(-0.0549999997,-0.0549999997,-0.0549999997,-0.0549999997);
  r2.xyzw = cmp(float4(0.0469620004,0.0469620004,0.0469620004,0.0469620004) >= r1.yyzw);
  r1.xyzw = float4(0.86133337,0.86133337,0.86133337,0.86133337) * r1.xyzw;
  r0.xyzw = r2.xyzw ? r1.xyzw : r0.xyzw;
  r1.xyzw = saturate(r0.yyzw);
  r2.xyw = float3(14.9998999,0.9375,0.05859375) * r1.xwz;
  r1.x = floor(r2.x);
  r2.x = r1.x * 0.0625 + r2.w;
  r2.xyzw = float4(0.001953125,0.03125,0.064453125,0.03125) + r2.xyxy;
  r1.x = r1.y * 15 + -r1.x;
  r1.yzw = g_Texture1.Sample(g_Texture1Sampler_s, r2.zw).xyz;
  r3.xyz = g_Texture1.Sample(g_Texture1Sampler_s, r2.xy).xyz;
  r1.yzw = -r3.xyz + r1.yzw;
  r1.yzw = r1.xxx * r1.yzw + r3.xyz;
  r3.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r2.zw).xyz;
  r2.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r2.xy).xyz;
  r3.xyz = r3.xyz + -r2.xyz;
  r2.xyz = r1.xxx * r3.xyz + r2.xyz;
  r1.xyz = -r2.xyz + r1.yzw;
  r1.xyz = g_ExposureParam.zzz * r1.xyz + r2.xyz;
  r1.xyz = log2(abs(r1.xyz));
  r1.xyz = g_Brightness.xxx * r1.xyz;
  o0.xyz = exp2(r1.xyz);
  o0.w = 1;
  r1.xyw = float3(14.9998999,0.9375,0.05859375) * r0.xwz;
  r0.x = floor(r1.x);
  r0.y = r0.y * 15 + -r0.x;
  r1.x = r0.x * 0.0625 + r1.w;
  r1.xyzw = float4(0.001953125,0.03125,0.064453125,0.03125) + r1.xyxy;
  r0.xzw = g_Texture1.Sample(g_Texture1Sampler_s, r1.zw).xyz;
  r2.xyz = g_Texture1.Sample(g_Texture1Sampler_s, r1.xy).xyz;
  r0.xzw = -r2.xyz + r0.xzw;
  r0.xzw = r0.yyy * r0.xzw + r2.xyz;
  r2.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r1.zw).xyz;
  r1.xyz = g_Texture3.Sample(g_Texture3Sampler_s, r1.xy).xyz;
  r2.xyz = r2.xyz + -r1.xyz;
  r1.xyz = r0.yyy * r2.xyz + r1.xyz;
  r0.xyz = -r1.xyz + r0.xzw;
  r0.xyz = g_ExposureParam.zzz * r0.xyz + r1.xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = g_Brightness.xxx * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r1.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r0.xyz;
  r1.xyz = float3(0.947867334,0.947867334,0.947867334) * r1.xyz;
  r1.xyz = log2(r1.xyz);
  r1.xyz = float3(2.4000001,2.4000001,2.4000001) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r2.xyz = cmp(float3(0.0392800011,0.0392800011,0.0392800011) >= r0.xyz);
  r0.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
  o1.xyz = r2.xyz ? r0.xyz : r1.xyz;
  o1.w = 1;
}