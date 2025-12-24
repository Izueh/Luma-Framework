#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

SamplerState sampler_tex_s : register(s0);
Texture2D<float4> tex : register(t0);

void main(
  float4 v0 : COLOR0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = tex.Sample(sampler_tex_s, v1.xy).xyzw;
  o0.w = v0.w * r0.w;
  o0.xyz = r0.xyz;
  
#if ENABLE_HDR_BOOST
  uint2 size;
  tex.GetDimensions(size.x, size.y);
  if (size.x == 1280 && size.y == 640) // All loading screens are of this size, make them HDR!
  {
    o0.rgb = gamma_sRGB_to_linear(o0.rgb, GCT_MIRROR); // Assume "VANILLA_ENCODING_TYPE" sRGB here
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
    o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR);
  }
#endif
}