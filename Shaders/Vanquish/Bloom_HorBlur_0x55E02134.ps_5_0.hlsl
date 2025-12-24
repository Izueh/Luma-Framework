#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

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
  float aspectRatioScale = 1.0;
#if 1 // Luma: fix bloom scaling in UW
  // Bloom textures were compressed to a ~16:9 texture in UW, so e.g. if we are at 32:9 we need to halve the horizontal UV offsets to match the 16:9 look
  // TODO: this should theoretically be adjusted to the rendering resolution, that is locked at 16:9 unless we run the game with the "-unlockaspectratio -fov 100" commands and ideally use a mod to fix the UI stretch. Otherwise we can get the rendering res from the shader 0x8B6A8D61 render target and use that to do scaling.
  aspectRatioScale = (16.0 / 9.0) / (LumaSettings.SwapchainSize.x * LumaSettings.SwapchainInvSize.y);
#endif

  float4 r0,r1;
  r0.xy = float2(0.0109374998 * aspectRatioScale,0) + v5.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));
  r1.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
  r1.xyzw = asfloat(asint(r1.xyzw) & asint(cb3[44].xyzw));
  r1.xyzw = asfloat(asint(r1.xyzw) | asint(cb3[45].xyzw));
  r1.xyzw = r0.xyzw * float4(0.449999988,0.449999988,0.449999988,0.449999988) + r1.xyzw;
  r0.xy = float2(0.00468750019 * aspectRatioScale,0) + v5.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));
  r1.xyzw = r0.xyzw * float4(0.800000012,0.800000012,0.800000012,0.800000012) + r1.xyzw;
  r0.xy = float2(-0.0109374998 * aspectRatioScale,-0) + v5.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));
  r1.xyzw = r0.xyzw * float4(0.449999988,0.449999988,0.449999988,0.449999988) + r1.xyzw;
  r0.xy = float2(-0.00468750019 * aspectRatioScale,-0) + v5.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));
  r0.xyzw = r0.xyzw * float4(0.800000012,0.800000012,0.800000012,0.800000012) + r1.xyzw;
  r0.xyzw = float4(0.285714298,0.285714298,0.285714298,0.285714298) * r0.xyzw;
  r0.xyz = r0.xyz * r0.w;
  o0.xyz = cb4[192].xyz * r0.xyz;
  o0.w = cb4[192].w;
  
#if 1 // Luma: emulate UNORM
  o0.a = saturate(o0.a);
  o0.rgba = max(o0.rgba, 0.0);
#if 0 // Clip highlights as it would have been in vanilla, oterwise bloom gets insanely bright
  o0.xyz = saturate(o0.xyz);
#elif 1 // Tonemap bloom...
  o0.xyz /= max(max3(o0.xyz), 1.0);
#endif
#endif
}