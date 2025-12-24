#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

// Note: this draws some black bars too but it's not really relevant as they are in 16:9 menus
// There's a chance it also draws some fades to black or something.
void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = dot(cb0[1].xyzw, cb0[1].xyzw);
  r0.x = r0.x != 0.0;
  r1.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.y = dot(r1.xyzw, cb0[1].xyzw);
  r0.xyzw = r0.xxxx ? r0.yyyy : r1.xyzw;
  r0.w = dot(r0.xyzw, cb0[2].xyzw);
  o0.xyzw = v1.xyzw * r0.xyzw;

  // Luma: fix UI negative values to emulate UNORM blends
  o0.w = saturate(o0.w);
  o0.xyz = max(o0.xyz, 0.f);

// Luma: this was the only UI element that was using a sRGB view, and thus writing in linear. Luma replaces the UI render target but kept it drawing in gamma space on a float RT, so it can't use sRGB views, thus linearize this before out.
// Edit: we simply added this one to the list of shaders excluded from the UI, it shouldn't draw in with the UI brightness multiplier anyway!
#if UI_DRAW_TYPE == 3 && 0
  o0.xyz = linear_to_sRGB_gamma(o0.xyz);
#endif
}