#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/DICE.hlsl"

#ifndef ENABLE_FAKE_HDR
#define ENABLE_FAKE_HDR 1
#endif

Texture2D<float4> Tex : register(t0);

void main(
  float4 v0 : SV_Position0,
  out float4 outColor : SV_Target0)
{
  outColor.xyzw = Tex.Load(int3(v0.xy, 0)).xyzw;

#if 1 // In the original game, the texture we sample here would have been UNORM_SRGB but sampled as a UNORM view here, thus implicitly converting to gamma space, we need to do it manually with float textures. This is duplicated in the FXAA shader too! And in the pause menu background.
  if (LumaData.CustomData1) // If drawing on swapchain!
  {
    float2 size;
    Tex.GetDimensions(size.x, size.y);
    float2 uv = v0.xy / size.xy;
    bool doHDR = !ShouldForceSDR(uv) && LumaSettings.DisplayMode == 1;
    // Do tonemapping here so it doesn't influence any anti aliasing, or auto exposure etc
    if (doHDR)
    {
#if ENABLE_FAKE_HDR
      float normalizationPoint = 0.025; // Found empyrically
      float fakeHDRIntensity = 0.1; // Hardcoded for now, no other value looked balance so there's not much need to expose it
      float fakeHDRSaturation = LumaSettings.GameSettings.HDRBoostSaturationAmount;
      outColor.rgb = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(outColor.rgb), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif
      
      const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
      const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
      // Fire is:
      // DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE: ~pinkish (looks best)
      // DICE_TYPE_BY_LUMINANCE_PQ: ~yellow
      // DICE_TYPE_BY_CHANNEL_PQ: ~white
      DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      outColor.rgb = DICETonemap(outColor.rgb * paperWhite, peakWhite, settings) / paperWhite;
    }

#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
	  ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, VANILLA_ENCODING_TYPE, min(GAMMA_CORRECTION_TYPE, 1), true); // Clamp "GAMMA_CORRECTION_TYPE" to 1 as values above aren't supported by these funcs but they are similar enough
    outColor.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	  ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, min(GAMMA_CORRECTION_TYPE, 1), VANILLA_ENCODING_TYPE, true); // TODO: this isn't mirrored for "GAMMA_CORRECTION_TYPE" > 1 in case it was used (3 looks best, after 0, in this game)
#endif

    outColor.xyz = linear_to_sRGB_gamma(outColor.xyz, GCT_MIRROR);
  }
#endif
}