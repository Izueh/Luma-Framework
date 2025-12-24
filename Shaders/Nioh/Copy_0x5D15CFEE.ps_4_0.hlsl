#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

cbuffer _Globals : register(b0)
{
  float vATest : packoffset(c0);
}

SamplerState smp_s : register(s0);
Texture2D<float4> tex : register(t0);

// Videos are decoded on the CPU, this simply plays them back on the UI
void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  if ((-vATest + v1.w) < 0) discard; // Black bars or something
  float4 r0 = tex.Sample(smp_s, v2.xy).xyzw;
  o0.xyz = v1.xyz * r0.xyz;
  o0.w = v1.w;
  
  if (LumaData.CustomData1) // Luma: this means this copy shader is targeting a video
  {
#if ENABLE_HDR_BOOST
    // Luma: add HDR extrapolation on videos
    if (LumaSettings.DisplayMode == 1)
    {
      o0.rgb = gamma_sRGB_to_linear(o0.rgb, GCT_MIRROR); // Assume "VANILLA_ENCODING_TYPE" sRGB here
      o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
      o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR);
    }
#endif

#if UI_DRAW_TYPE == 2 // This is drawn in the UI phase but it's not exactly classifiable UI, so make sure it scales with the game brightness instead
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, min(GAMMA_CORRECTION_TYPE, 1), true); // Clamp "GAMMA_CORRECTION_TYPE" to 1 as values above aren't supported by these funcs but they are similar enough
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, min(GAMMA_CORRECTION_TYPE, 1), VANILLA_ENCODING_TYPE, true);
#endif
  }
}