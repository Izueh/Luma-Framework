#define FXAA_HLSL_5 1
#if 1 // Optional: force max quality (otherwise it falls back on the default)
#define FXAA_QUALITY__PRESET 39
#endif

#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/FXAA.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef ENABLE_FXAA
#define ENABLE_FXAA 1
#endif

#ifndef IMPROVED_TONEMAPPING_TYPE
#define IMPROVED_TONEMAPPING_TYPE 1
#endif

SamplerState __smpsScreen_s : register(s0); // Linear sampler
Texture2D<float4> sScreen : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 uv = v0.xy * LumaSettings.SwapchainInvSize;
  uv = v1.xy; // The original one

#if ENABLE_FXAA
  FxaaTex tex;
  tex.tex = sScreen; // We store the perceptually encoded luminance in the alpha channel!
  tex.smpl = __smpsScreen_s;
  FxaaFloat2 fxaaQualityRcpFrame = LumaSettings.SwapchainInvSize;
  FxaaFloat fxaaQualitySubpix = FxaaFloat(0.75);
#if FXAA_QUALITY__PRESET >= 39
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.125); // Increase default quality
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0312); // Increase default quality
#else
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.166);
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0833);
#endif

  // The 0 params are console exclusive
  float4 r0 = FxaaPixelShader(
    uv,
    0.0,
    tex,
    tex,
    tex,
    fxaaQualityRcpFrame,
    0.0,
    0.0,
    0.0,
    fxaaQualitySubpix,
    fxaaQualityEdgeThreshold,
    fxaaQualityEdgeThresholdMin).xyzw;
#else
  float4 r0 = sScreen.Sample(__smpsScreen_s, uv).xyzw;
#endif

  bool doHDR = !ShouldForceSDR(uv) && LumaSettings.DisplayMode == 1;
  if (doHDR)
  {
#if ENABLE_HDR_BOOST
    float normalizationPoint = 0.025;
    float fakeHDRIntensity = 0.275;
#if IMPROVED_TONEMAPPING_TYPE >= 2
    // If we had tonemapped by luminance, as opposed to by channel as in Vanilla SDR, boost saturation more to recover it!!!
    // Extreme values still look good in this game.
    float fakeHDRSaturation = 0.4;
#else
    // We already tonemapper by channel in BT.2020 so we have enough saturation
    float fakeHDRSaturation = 0.15;
#endif
    r0.rgb = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(r0.rgb), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif

    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    r0.rgb = DICETonemap(r0.rgb * paperWhite, peakWhite, settings) / paperWhite;
  }
  
  // Convert to gamma space before UI rendering and swapchain copy
#if VANILLA_ENCODING_TYPE == 0 // Original code
  o0.xyz = linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR); // Luma: added sRGB mirroring!
#else // Luma: try gamma 2.2 (this branch shouldn't really be here?)
  o0.xyz = linear_to_gamma(r0.xyz, GCT_MIRROR);
#endif

#if ENABLE_FXAA
  o0.w = 0; // With FXAA this would still be the luminance, so force it to 0 (a default neutral value) in case it was ever used by the UI
#else
  o0.w = r0.w;
#endif
  
#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, min(GAMMA_CORRECTION_TYPE, 1), true); // Clamp "GAMMA_CORRECTION_TYPE" to 1 as values above aren't supported by these funcs but they are similar enough
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, min(GAMMA_CORRECTION_TYPE, 1), VANILLA_ENCODING_TYPE, true);
#endif
}