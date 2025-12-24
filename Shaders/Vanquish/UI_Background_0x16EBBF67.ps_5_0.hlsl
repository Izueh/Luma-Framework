#include "../Includes/Common.hlsl"

Texture2D<float4> t13 : register(t13);

SamplerState s13_s : register(s13);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = t13.Sample(s13_s, v5.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[70].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[71].xyzw));
#if 1 // Luma: remove ugly clipping and optionally do tonemapping + scaling instead
  r0.xyz *= cb4[73].xyz;
#elif 1
  r0.xyz = (r0.xyz / max(max3(r0.xyz), 1.0)) * cb4[73].xyz;
#else
  r0.xyz = saturate(min(r0.xyz, cb4[73].xyz)); // Luma: remove saturate
#endif
  o0.xyz = cb4[72].xyz * r0.xyz;
  o0.w = cb4[72].w;
}