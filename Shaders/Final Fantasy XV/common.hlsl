#include "GameCBuffers.hlsl"
#include "../Includes/Common.hlsl"

#include "../Includes/Color.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Oklab.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/Tonemap.hlsl"


float3 CorrectHuePolar(float3 incorrectOkLCH, float3 correctOkLCH, float strength) {
  // skip adjustment for achromatic colors
  const float chromaThreshold = 1e-5;
  float iChroma = incorrectOkLCH.y;
  float cChroma = correctOkLCH.y;

  if (iChroma < chromaThreshold || cChroma < chromaThreshold) {
    return incorrectOkLCH;
  }

  // hues in radians
  float iHue = incorrectOkLCH.z;
  float cHue = correctOkLCH.z;

  // calculate shortest angular difference
  float diff = cHue - iHue;
  if (diff > PI) diff -= PI_X2;
  else if (diff < -PI) diff += PI_X2;

  // apply strength-based correction
  float newHue = iHue + strength * diff;

  float3 adjustedOkLCH = float3(
      incorrectOkLCH.x,
      incorrectOkLCH.y,
      newHue
  );

  return adjustedOkLCH;
}


#define CUSTOM_TONEMAP_UPGRADE_HUECORR 1.0f
#define CUSTOM_TONEMAP_UPGRADE_STRENGTH 0.4f


float UpgradeToneMapRatio(float color_hdr, float color_sdr, float post_process_color) {
  if (color_hdr < color_sdr) {
    // If substracting (user contrast or paperwhite) scale down instead
    // Should only apply on mismatched HDR
    return color_hdr / color_sdr;
  } else {
    float delta = color_hdr - color_sdr;
    delta = max(0, delta);  // Cleans up NaN
    const float new_value = post_process_color + delta;

    const bool valid = (post_process_color > 0);  // Cleans up NaN and ignore black
    return valid ? (new_value / post_process_color) : 0;
  }
}


float3 UpgradeToneMapPerChannel(float3 color_hdr, float3 color_sdr, float3 post_process_color, float post_process_strength) {
  // float ratio = 1.f;

  float3 bt2020_hdr = max(0, BT709_To_BT2020(color_hdr));
  float3 bt2020_sdr = max(0, BT709_To_BT2020(color_sdr));
  float3 bt2020_post_process = max(0, BT709_To_BT2020(post_process_color));

  float3 ratio = float3(
      UpgradeToneMapRatio(bt2020_hdr.r, bt2020_sdr.r, bt2020_post_process.r),
      UpgradeToneMapRatio(bt2020_hdr.g, bt2020_sdr.g, bt2020_post_process.g),
      UpgradeToneMapRatio(bt2020_hdr.b, bt2020_sdr.b, bt2020_post_process.b));

  float3 color_scaled = max(0, bt2020_post_process * ratio);
  color_scaled = BT2020_To_BT709(color_scaled);
  float peak_correction = saturate(1.f - GetLuminance(bt2020_post_process, CS_BT2020));
  color_scaled = RestoreHueAndChrominance(color_scaled, post_process_color, peak_correction, 0.0, 0.0, FLT_MAX, 0.0, CS_BT2020);
  return lerp(color_hdr, color_scaled, post_process_strength);
}



float3 CustomUpgradeToneMapPerChannel(float3 untonemapped, float3 graded) {
  float hueCorrection = 1.f - CUSTOM_TONEMAP_UPGRADE_HUECORR;
  float satStrength = 1.f - CUSTOM_TONEMAP_UPGRADE_STRENGTH;

  ReinhardSettings settings = DefaultReinhardSettings();
  settings.by_luminance = true;

  float3 upgradedPerCh = UpgradeToneMapPerChannel(
      untonemapped,

      // use the neutral Reinhard as the neutral SDR
      ReinhardTonemap(untonemapped, 100.f, 100.f, settings),
      graded,
      1.f);

  float3 upgradedPerCh_okLCH = Oklab::linear_srgb_to_oklch(upgradedPerCh);
  float3 graded_okLCH = Oklab::linear_srgb_to_oklch(graded);

  // heavy hue correction with graded hue
  upgradedPerCh_okLCH = CorrectHuePolar(upgradedPerCh_okLCH, graded_okLCH, saturate(pow(graded_okLCH.y, hueCorrection)));

  // desaturate highlights based on graded chrominance
  upgradedPerCh_okLCH.y = lerp(graded_okLCH.y, upgradedPerCh_okLCH.y, saturate(pow(graded_okLCH.y, satStrength)));

  upgradedPerCh = Oklab::oklch_to_linear_srgb(upgradedPerCh_okLCH);

  upgradedPerCh = max(-10000000000000000000000000000000000000.f, upgradedPerCh);  // bandaid for NaNs

  return upgradedPerCh;
}


float3 NeutralSDR(float3 color)    {

    ReinhardSettings settings = DefaultReinhardSettings();
    settings.by_luminance = true;
    return ReinhardTonemap(color, 100.f, 100.f, settings);
}

float3 ToneMapReinhard(float3 color, bool per_channel = true)    {

    ReinhardSettings settings = DefaultReinhardSettings();
    settings.by_luminance = !per_channel;
    return ReinhardTonemap(color, LumaSettings.PeakWhiteNits, LumaSettings.GamePaperWhiteNits, settings);
}



float3 GammaCorrection(float3 color, float gamma=2.4)   {

    float3 colorSign = sign(color);

    return colorSign * gamma_to_linear(linear_to_sRGB_gamma(color, GCT_NONE), GCT_NONE, gamma);

}



float3 RestoreHighlightSaturation(float3 untonemapped)	{

	float l = GetLuminance(untonemapped, CS_BT709);

	DICESettings settings = DefaultDICESettings();
	settings.Type = DICE_TYPE_BY_LUMINANCE_RGB;
	settings.ShoulderStart = 0.f;
	float3 displayMappedColor = DICETonemap(untonemapped, 1.0f, settings);

	float3 output = lerp(untonemapped, displayMappedColor, saturate(l));

	return output;
}