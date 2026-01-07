#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = t1.Sample(s0_s, v1.zw).xyzw;
  r1.xyzw = t0.Sample(s0_s, v1.zw).xyzw;
  r0.xyzw -= r1.xyzw;
  r2.xy = t2.Sample(s0_s, v1.xy).xy;
  r0.xyzw = r2.y * r0.xyzw + r1.xyzw;
  o0.w = r0.w * r2.x;
  o0.xyz = r0.xyz;

#if ENABLE_AUTO_HDR
  o0.rgb = gamma_to_linear(o0.rgb); // Support for negative values is likely not needed
  o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits, 2.25);
  o0.rgb = linear_to_gamma(o0.rgb);
#endif

#if UI_DRAW_TYPE == 2 // This is drawn in the UI phase but it's not really UI, so make sure it scales with the game brightness instead
  bool linearEncoding = false;
  ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, linearEncoding);
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
  ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, linearEncoding);
#endif

#if 0 // Actually not needed, it was writing from to non sRGB views (gamma to gamma), though we might need some custom handling if we upgraded its textures too
  o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR); // Needed because the original view was a R8G8B8A8_UNORM_SRGB, with the input being float/linear, so there was an implicit sRGB encoding.
#endif
}