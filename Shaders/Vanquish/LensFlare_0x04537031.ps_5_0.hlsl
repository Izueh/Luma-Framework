#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

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
  float4 r0,r1,r2;
  r0.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));
  r1.xyzw = r0.w * v6.w - 0.001;
  r0.xyzw = v6.xyzw * r0.xyzw;
  r2.xyz = (r1.xyz < 0);
  r2.x = asfloat(asint(r2.y) | asint(r2.x));
  r2.x = asfloat(asint(r2.z) | asint(r2.x));
  if (r2.x != 0) discard;
  o0.xyzw = r0.xyzw;
#if ENABLE_HDR_BOOST && 0 // Luma: boost them up!!! Even in SDR (because we now do TM) // TODO: expose? This isn't reliable here as this shader is used for too many things
  o0.xyz *= 1.25;
#endif
  o0.w = saturate(o0.w); // Luma: emulate UNORM // TODO: there's a few more particles with the same issue...
}