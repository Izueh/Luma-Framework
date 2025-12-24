#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

SamplerState VideoLuma_s : register(s0);
SamplerState VideoCr_s : register(s1);
SamplerState VideoCb_s : register(s2);
Texture2D<float4> VideoLuma : register(t0);
Texture2D<float4> VideoCr : register(t1);
Texture2D<float4> VideoCb : register(t2);

// For some reason there's 2 near identical video shaders
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  float Cr = VideoCr.Sample(VideoCr_s, v1.xy).x;
  float Y = VideoLuma.Sample(VideoLuma_s, v1.xy).x;
  float Cb = VideoCb.Sample(VideoCb_s, v1.xy).x;

#if 1 // Luma: fixed color space (it was using limited BT.601 but it was full BT.709)
  o0.xyz = YUVtoRGB(Y, Cr, Cb, 0);
#if 1 // Emulate the constrast boost from accidentally interepreting them as limited, but without clipping
  o0.rgb = EmulateShadowClip(o0.rgb, false, 0.15);
#endif
#else
  o0.xyz = Cr * float3(1.59500003,-0.813000023,0) + Y * float3(1.16400003,1.16400003,1.16400003) + Cb * float3(0,-0.391000003,2.01699996) + float3(-0.870000005,0.528999984,-1.08159995);
#endif

#if ENABLE_HDR_BOOST
  o0.rgb = gamma_sRGB_to_linear(o0.rgb, GCT_MIRROR); // Assume "VANILLA_ENCODING_TYPE" sRGB here
  o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
  o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR);
#endif

#if UI_DRAW_TYPE == 2 // This is drawn in the UI phase but it's not exactly classifiable UI, so make sure it scales with the game brightness instead
  ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
  ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

#if DEVELOPMENT // TODO: test vids
  o0.xyz = float3(1, 0, 1);
#endif

  o0.w = 1;
}