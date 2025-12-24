cbuffer HPixel_Buffer : register(b12)
{
  float4 g_TargetUvParam : packoffset(c0);
}

SamplerState g_TexColorHDRSampler_s : register(s0);
SamplerState g_TexWeightSampler_s : register(s1);
SamplerState g_TexSDRSampler_s : register(s2);
Texture2D<float4> g_TexColorHDR : register(t0);
Texture2D<float4> g_TexWeight : register(t1);
Texture2D<float4> g_TexSDR : register(t2);

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2;
  r0.yz = float2(0,0);
  r1.w = g_TexWeight.SampleLevel(g_TexWeightSampler_s, v1.xy, 0, int2(1, 0)).w;
  r1.xz = g_TexWeight.SampleLevel(g_TexWeightSampler_s, v1.xy, 0).xz;
  r2.x = cmp(r1.z < r1.w);
  r0.x = r2.x ? r1.w : -r1.z;
  r1.y = g_TexWeight.SampleLevel(g_TexWeightSampler_s, v1.xy, 0, int2(0, 1)).y;
  r2.x = cmp(r1.x < r1.y);
  r0.w = r2.x ? r1.y : -r1.x;
  r1.x = dot(r1.xyzw, float4(1,1,1,1));
  r1.x = cmp(9.99999975e-006 < r1.x);
  r1.y = cmp(abs(r0.w) < abs(r0.x));
  r0.xy = r1.yy ? r0.xy : r0.zw;
  r0.xy = g_TargetUvParam.xy * r0.xy;
  r0.xy = r0.xy * float2(2,2) + v1.xy;
  r0.xy = r1.xx ? r0.xy : v1.xy;
#if 0 // Luma: disabled a wasteful read/write
  o0.xyzw = g_TexSDR.SampleLevel(g_TexSDRSampler_s, r0.xy, 0).xyzw;
#else
  o0.xyzw = 0;
#endif
  o1.xyzw = g_TexColorHDR.SampleLevel(g_TexColorHDRSampler_s, r0.xy, 0).xyzw;
}