#include "Includes/Common.hlsl"
#include "../Includes/Reinhard.hlsl"

Texture2D<float4> t1 : register(t1); // Scene
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  const float4 icb[] = { { 0, 0, 0.250000, 0},
                              { -0.852561, 0.852561, 0.062500, 0},
                              { 0, 0.875000, 0.125000, 0},
                              { 0.852561, 0.852561, 0.062500, 0},
                              { -0.875000, 0, 0.125000, 0},
                              { 0.875000, 0, 0.125000, 0},
                              { -0.852561, -0.852561, 0.062500, 0},
                              { 0, -0.875000, 0.125000, 0},
                              { 0.852561, -0.852561, 0.062500, 0} };
  float4 r0,r1,r2,r3,r4,r5;

  r0.xyzw = t0.Sample(s1_s, v1.xy).xyzw;
  r1.xyzw = t1.Sample(s0_s, v1.xy).xyzw;
#if !ENABLE_LUMA
  r1.xyzw = saturate(r1.xyzw);
#endif
  r0.y = 0.629960537 + r1.w;
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y + 0.842509866;
  r0.yzw = r1.xyz * r0.yyy;
  r0.yzw = float3(0.25,0.25,0.25) * r0.yzw;
  r2.xyz = r0.yzw;
  int4 r2i;
  r2i.w = 1;
  while (true) {
    if (r2i.w >= 9) break;
    r3.xy = cb0[9].zw * icb[r2i.w+0].xy + v1.xy;
    r3.xyzw = t1.Sample(s0_s, r3.xy).xyzw;
    r3.w = 0.629960537 + r3.w;
    r3.w = r3.w * r3.w;
    r3.w = r3.w * r3.w + 0.842509866;
    r3.xyz = r3.xyz * r3.www;
    r2.xyz = icb[r2i.w+0].zzz * r3.xyz + r2.xyz;
    r2i.w++;
  }
  r0.yzw = -cb0[8].xxx + r2.xyz;
  r0.yzw = max(float3(0,0,0), r0.yzw);
  r1.w = cmp(r1.w < 0.0196078438);
  r0.yzw = r1.www ? float3(0,0,0) : r0.yzw;
  r1.w = 0.673900008 + cb1[1].x;
  r2.xy = v1.xy + r1.ww;
  r2.xy = float2(5.39870024,5.44210005) * r2.xy;
  r2.xy = frac(r2.xy);
  r2.zw = float2(21.5351009,14.3136997) + r2.xy;
  r1.w = dot(r2.yx, r2.zw);
  r2.xy = r2.xy + r1.ww;
  r1.w = r2.x * r2.y;
  r2.xyz = float3(95.4307022,97.5901031,93.8368988) * r1.www;
  r2.xyz = frac(r2.xyz);
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = cb0[8].zzz * r2.xyz;
  r2.xyz = float3(0.5,0.5,0.5) * r2.xyz;
#if !ENABLE_DITHER
  r2.xyz = 0;
#endif
  r1.xyz = -cb0[7].www + r1.xyz;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r1.xyz = r1.xyz * r0.xxx;
  r3.xyz = float3(0.5,0.5,0.5) * r1.xyz;
  r4.xyz = asfloat(asint(r1.xyz) >> 1);
  r4.xyz = asfloat(-asint(r4.xyz) + int3(0x5f375a86,0x5f375a86,0x5f375a86));
  r5.xyz = r4.xyz * r4.xyz;
  r3.xyz = -r3.xyz * r5.xyz + float3(1.5,1.5,1.5);
  r3.xyz = r4.xyz * r3.xyz;
  o0.xyz = r1.xyz * r3.xyz + r2.xyz;
  r1.xyz = r0.yzw * r0.xxx;
  r0.xyz = r0.yzw * r0.xxx + float3(1,1,1);
  r0.xyz = asfloat(-asint(r0.xyz) + int3(0x7ef311c2,0x7ef311c2,0x7ef311c2));
  o1.xyz = r1.xyz * r0.xyz + r2.xyz;
  o0.w = 1;
  o1.w = 1;

#if ENABLE_LUMA // Luma: pre-tonemap bloom and emissive outputs for consistency with SDR
  // Tonemap from ~2.0 to 1.0, starting from ~0.5. In gamma space because whatever. This should only really affect highlights.
  o0.xyz = Reinhard::ReinhardRange(o0.xyz, 0.333, 2.0, 1.0);
  // Emissive was usually small already, and looks ugly when too bright, so compress it more.
  o1.xyz = Reinhard::ReinhardSimple(o1.xyz, 0.75); // This cannot be beyond >=1 or it will break in the emissige generation dithering blue noise code
#else // TODO: bloom seems stronger even if we set "ENABLE_LUMA" to false? Maybe we need to clamp it in more places?
  o0.xyz = saturate(o0.xyz);
  o1.xyz = saturate(o1.xyz);
#endif
}