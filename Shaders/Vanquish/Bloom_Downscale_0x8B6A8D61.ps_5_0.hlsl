#include "../Includes/Common.hlsl"

#ifndef ENABLE_IMPROVED_BLOOM
#define ENABLE_IMPROVED_BLOOM 1
#endif

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
  float4 r0;

// Luma: bloom was downscaled from the source resolution to a fixed size of 320x176, independently from the aspect ratio..., making bloom very low quality and unstable
// TODO: just make n mips of the source texture and sample that... Also I'm not 100% certain the target res is always that. As of now we still skip some samples, so it's not perfect, but it shall do!!!
#if ENABLE_IMPROVED_BLOOM

  float2 sourceSize;
  t0.GetDimensions(sourceSize.x, sourceSize.y);
  // This shader is run multiple times, so only correct the first downscale, the others are fine
  if (LumaSettings.SwapchainSize.x == sourceSize.x && LumaSettings.SwapchainSize.y == sourceSize.y)
  {
    // Size in source texels that one 320x176 output pixel covers:
    float2 footprintTexels = LumaSettings.SwapchainSize / float2(320.0, 176.0);

    // Centered 4-tap positions within that footprint:
    // (-3/8, -1/8, +1/8, +3/8) of the footprint (in *texels*).
    float2 tap0 = (-0.375).xx * footprintTexels;
    float2 tap1 = (-0.125).xx * footprintTexels;
    float2 tap2 = ( 0.125).xx * footprintTexels;
    float2 tap3 = ( 0.375).xx * footprintTexels;

    const float2 o00 = float2(tap0.x, tap0.y) * LumaSettings.SwapchainInvSize;
    const float2 o10 = float2(tap1.x, tap0.y) * LumaSettings.SwapchainInvSize;
    const float2 o20 = float2(tap2.x, tap0.y) * LumaSettings.SwapchainInvSize;
    const float2 o30 = float2(tap3.x, tap0.y) * LumaSettings.SwapchainInvSize;

    const float2 o01 = float2(tap0.x, tap1.y) * LumaSettings.SwapchainInvSize;
    const float2 o11 = float2(tap1.x, tap1.y) * LumaSettings.SwapchainInvSize;
    const float2 o21 = float2(tap2.x, tap1.y) * LumaSettings.SwapchainInvSize;
    const float2 o31 = float2(tap3.x, tap1.y) * LumaSettings.SwapchainInvSize;

    const float2 o02 = float2(tap0.x, tap2.y) * LumaSettings.SwapchainInvSize;
    const float2 o12 = float2(tap1.x, tap2.y) * LumaSettings.SwapchainInvSize;
    const float2 o22 = float2(tap2.x, tap2.y) * LumaSettings.SwapchainInvSize;
    const float2 o32 = float2(tap3.x, tap2.y) * LumaSettings.SwapchainInvSize;

    const float2 o03 = float2(tap0.x, tap3.y) * LumaSettings.SwapchainInvSize;
    const float2 o13 = float2(tap1.x, tap3.y) * LumaSettings.SwapchainInvSize;
    const float2 o23 = float2(tap2.x, tap3.y) * LumaSettings.SwapchainInvSize;
    const float2 o33 = float2(tap3.x, tap3.y) * LumaSettings.SwapchainInvSize;

    float4 sum = 0.0;
    sum += t0.Sample(s0_s, v5.xy + o00).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o10).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o20).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o30).xyzw;

    sum += t0.Sample(s0_s, v5.xy + o01).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o11).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o21).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o31).xyzw;

    sum += t0.Sample(s0_s, v5.xy + o02).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o12).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o22).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o32).xyzw;

    sum += t0.Sample(s0_s, v5.xy + o03).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o13).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o23).xyzw;
    sum += t0.Sample(s0_s, v5.xy + o33).xyzw;

    r0.xyzw = sum * (1.0 / 16.0);
  }
  else
  {
    r0.xyzw = t0.Sample(s0_s, v5.xy).xyzw;
  }

#else

  r0.xyzw = t0.Sample(s0_s, v5.xy).xyzw;

#endif

  // Useless filtering?
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[44].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[45].xyzw));

  // Luma: fixed BT.601 luminance (and doing it in gamma space)
  float luminance = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_POSITIVE)));
  r0.w = r0.w * cb4[192].x + luminance;
  r0.w = -cb4[192].y + r0.w;
  o0.w = cb4[192].w * r0.w;
  
  o0.xyz = r0.xyz + r0.xyz; // Double brightness

#if 1 // Luma: emulate UNORM
  o0.a = saturate(o0.a);
  o0.rgba = max(o0.rgba, 0.0);
#if 0 // Clip highlights as it would have been in vanilla, oterwise bloom gets insanely bright
  o0.xyz = saturate(o0.xyz);
#elif 1 // Tonemap bloom...
  o0.xyz /= max(max3(o0.xyz), 1.0); // TODO: desaturate it a bit too if it's too bright? Here and in the other shader that has the same code too!
#endif
#endif
}