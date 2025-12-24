#include "../Includes/Common.hlsl"

Texture2D<float4> t14 : register(t14);
Texture2D<float4> t13 : register(t13);

SamplerState s14_s : register(s14);
SamplerState s13_s : register(s13);

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

// Common UI shader used for sprites, text (?) or overlay/tint effects
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
  float4 r0,r1,r2,r3;
  r0.xyzw = t14.Sample(s14_s, v6.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[72].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[73].xyzw));
  r1.xyzw = t13.Sample(s13_s, v5.xy).xyzw;
  r1.xyzw = asfloat(asint(r1.xyzw) & asint(cb3[70].xyzw));
  r1.xyzw = asfloat(asint(r1.xyzw) | asint(cb3[71].xyzw));
  r2.xyzw = v7.xyzw;
  r1.xyzw = r1.xyzw * r2.xyzw + v8.xyzw;
  r3.w = r1.w * r0.w;
  r3.xyz = r1.xyz;
  r0.w = r3.w * 255.0 + 0.0001;
  r0.w = (asuint(cb3[8].z) >= (uint)r0.w);
  if (r0.w != 0) discard;
  o0.xyzw = r3.xyzw;
  
  // Luma: emulate UNORM
  o0.w = saturate(o0.w);
  o0.rgb = max(o0.rgb, 0.0);
}