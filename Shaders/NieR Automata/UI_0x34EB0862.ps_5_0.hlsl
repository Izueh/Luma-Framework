// ---- Created with 3Dmigoto v1.3.16 on Tue Dec 16 06:35:04 2025

SamplerState g_ColorTextureSampler_s : register(s0);
SamplerState g_MaskTextureSampler_s : register(s1);
Texture2D<float4> g_ColorTexture : register(t0);
Texture2D<float4> g_MaskTexture : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : COLOR0,
  float4 v4 : COLOR1,
  float4 v5 : COLOR2,
  float4 v6 : COLOR3,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = -v2.yy + v1.xy;
  r0.xy = -v2.zz + r0.xy;
  r0.xy = saturate(r0.xy / v2.ww);
  r0.z = cmp(v2.x >= 0.5);
  r0.z = r0.z ? 1.000000 : 0;
  r0.y = r0.y * r0.z;
  r0.w = cmp(-0.5 >= v2.x);
  r0.w = r0.w ? 1.000000 : 0;
  r0.x = r0.x * r0.w + r0.y;
  r0.y = r0.w + r0.z;
  r0.y = 1 + -r0.y;
  r1.xyzw = v5.xyzw + -v4.xyzw;
  r1.xyzw = r0.xxxx * r1.xyzw + v4.xyzw;
  r2.xyzw = v4.xyzw + -r1.xyzw;
  r0.xyzw = r0.yyyy * r2.xyzw + r1.xyzw;
  r1.xyzw = g_ColorTexture.Sample(g_ColorTextureSampler_s, v1.xy).xyzw;
  r2.x = r1.x + r1.y;
  r2.x = r2.x + r1.z;
  r2.x = r2.x * 0.333333343 + -r1.w;
  r2.yzw = cmp(v6.xyz >= float3(0.100000001,0.100000001,0.100000001));
  r2.yzw = r2.yzw ? float3(1,1,1) : 0;
  r1.w = r2.z * r2.x + r1.w;
  r2.x = 1 + -r1.w;
  r3.w = r2.w * r2.x + r1.w;
  r2.xzw = float3(1,1,1) + -r1.xyz;
  r3.xyz = r2.yyy * r2.xzw + r1.xyz;
  r1.xyzw = v3.xyzw * r3.xyzw;
  r2.xyzw = r1.xyzw * r0.xyzw;
  r0.xyz = r1.xyz * r0.xyz + float3(0.0549999997,0.0549999997,0.0549999997);
  r0.xyz = float3(0.947867334,0.947867334,0.947867334) * r0.xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = float3(2.4000001,2.4000001,2.4000001) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.w = g_MaskTexture.Sample(g_MaskTextureSampler_s, w1.xy).w;
  r1.x = r2.w * r0.w + -0.00392156886;
  r0.w = r2.w * r0.w;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  o0.xyz = r2.xyz;
  o0.w = r0.w;
  o1.w = r0.w;
  r1.xyz = cmp(float3(0.0392800011,0.0392800011,0.0392800011) >= r2.xyz);
  r2.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r2.xyz;
  o1.xyz = r1.xyz ? r2.xyz : r0.xyz;
  return;
}