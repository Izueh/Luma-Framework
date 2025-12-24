#include "../Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0); // sRGB

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

#define cmp

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
  r0.xyzw = t2.Sample(s2_s, v5.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[48].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[49].xyzw));
  r1.xyzw = t1.Sample(s1_s, v5.xy).xyzw;
  r1.xyzw = asfloat(asint(r1.xyzw) & asint(cb3[46].xyzw));
  r1.xyzw = asfloat(asint(r1.xyzw) | asint(cb3[47].xyzw));
  r2.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
  r2.rgb = gamma_sRGB_to_linear(r2.rgb, GCT_MIRROR);
#endif
  r2.xyzw = asfloat(asint(r2.xyzw) & asint(cb3[44].xyzw));
  r2.xyzw = asfloat(asint(r2.xyzw) | asint(cb3[45].xyzw));
  r2.xyz = r2.xyz * cb4[192].xyz + r1.xyz;
  r0.w = saturate(cb4[199].w * -r0.x + cb4[199].x);
  r0.xyz = cb4[198].xyz;
  r2.xyz = r2.xyz * cb4[193].xyz + -r0.xyz;
  r0.xyz = r0.w * r2.xyz + cb4[198].xyz;

#if 1 // Luma
  o0.xyz = linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR);
#else
  r3.y = log2(abs(r0.x));
  r3.x = cmp((int)r3.y == 0xff800000);
  r2.x = r3.x ? -9.99999993e+036 : r3.y;
  r3.y = log2(abs(r0.y));
  r3.x = cmp((int)r3.y == 0xff800000);
  r2.y = r3.x ? -9.99999993e+036 : r3.y;
  r3.y = log2(abs(r0.z));
  r3.x = cmp((int)r3.y == 0xff800000);
  r2.z = r3.x ? -9.99999993e+036 : r3.y;
  r2.xyz = float3(0.416666657,0.416666657,0.416666657) * r2.xyz;
  r2.xyz = exp2(r2.xyz);
  r1.xyz = r2.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r2.xyz = float3(0.00313080009,0.00313080009,0.00313080009) + -r0.xyz;
  r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
  r3.xyz = cmp(r2.xyz >= float3(0,0,0));
  o0.xyz = r3.xyz ? r0.xyz : r1.xyz;
#endif
  
  o0.w = r2.w;
}