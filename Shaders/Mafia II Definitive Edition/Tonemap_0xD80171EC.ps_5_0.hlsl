#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 2
#endif

#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

cbuffer _Globals : register(b0)
{
  row_major float4x4 g_SMapTM[4] : packoffset(c101);
  float4 g_DbgColor : packoffset(c117);
  float4 g_FilterTaps[8] : packoffset(c118);
  float4 g_FadingParams : packoffset(c126);
  float4 g_CSMRangesSqr : packoffset(c127);
  float2 g_SMapSize : packoffset(c128);
  float4 g_CameraOrigin : packoffset(c129);
  float4 gammaExpSatBleach : packoffset(c0);
}

SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  o0.w = 1;

  bool forceVanillaSDR = ShouldForceSDR(v1.xy);
  bool gammaSpace = true;

  float3 color = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyz;

  // Some exposure multiplier, it's usually neutral or so
  color *= exp2(gammaExpSatBleach.y);

  // Gamma (possibly the user brightness, but likely just a scene driven value!!!)
  color = pow(abs(color), gammaExpSatBleach.x) * Sign_Fast(color); // Luma: mirrored negative values

  float3 colorClamped = saturate(color); // Vanilla like color

  // Bleach. Not exactly sure what this does, it does some sort of contrast adjustment on highlights, but it doesn't tint the iamge much.
  float luminance = linear_to_gamma1(GetLuminance(gamma_to_linear(colorClamped, GCT_POSITIVE))); // Luma: calc luminance in linear space (on clamped rgb, for vanilla consistency)
  float3 invColor = 1.0 - float3(colorClamped);
  float invLuminance = 1.0 - luminance;
  float bleachAlpha = saturate((luminance - 0.45) * 10.0);
  float3 bleachAmount = lerp(luminance * colorClamped * 2.0, 1.0 - invColor * (invLuminance * 2.0), bleachAlpha);
  color += (-gammaExpSatBleach.w * colorClamped + 1.0) * (gammaExpSatBleach.w * colorClamped * bleachAmount); // Calculated on clamped color for vanilla consistency

#if ENABLE_COLOR_GRADING
  bool improvedSaturation = false;
#if IMPROVED_COLOR_GRADING_TYPE >= 1
  improvedSaturation = true;
#endif
  float3 colorGreyscaleOffset;
  if (forceVanillaSDR || !improvedSaturation)
  {
    colorGreyscaleOffset = color - average(color); // offset to grey scale (calculated using average instead of luminance, as that's how it was in vanilla) (not clamped so this can get even stronger in HDR!)
  }
  else // Luma: do saturation change in linear space to avoid deep frying colors
  {
    color = gamma_to_linear(color, GCT_MIRROR); 
    colorGreyscaleOffset = color - GetLuminance(color);
    gammaSpace = false;
  }
  colorGreyscaleOffset *= gammaExpSatBleach.z; // "gammaExpSatBleach.z" should be negative to actually desaturate, and positive to saturate.
  color += colorGreyscaleOffset; // Note: this can create negative values
  // TODO: this kinda desaturates highlights after they got saturated, possibly to prevent the sky from going too blue, or to constrain to the SDR range without clipping,
  // however, currently we don't emulate this behaviour in HDR. Overall it doesn't look bad, but unless we clamp "colorGreyscaleOffset" to a sensible range, we get NaNs or anyway errors from it.
  // Furthermore, it's simply not necessary in HDR as we have more range, and tonemapping happens after.
  // An alternative idea would be to apply less saturation boost on highlights to begin with, but that's not needed until proven otherwise.
  if (forceVanillaSDR || !improvedSaturation) // Let in the full sat/desat with Luma, it avoids NaN like issues with the division. Test the issue at the start the first DLC and check the reflection in the main character glasses, and the light on top of the door)
  {
    color /= max(colorGreyscaleOffset, -1.0 + FLT_EPSILON) + 1.0; // Semi normalize (possibly to keep in range, this partially undoes the effect, especially on highlights, though it also distorts them and sends them out of range if in HDR)
  }
#endif // ENABLE_COLOR_GRADING

  o0.xyz = color;

  // Luma tonemapping (this is the final shader before UI):
  
  if (gammaSpace)
  {
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
    gammaSpace = false;
  }
  
#if ENABLE_HDR_BOOST // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that (note that bloom is pre-tonemapped to avoid this blowing up)
  if (!forceVanillaSDR && LumaSettings.DisplayMode == 1)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.2;
    float saturationExpansionIntensity = 0.1;
#if DEVELOPMENT && 0 // TODO: tweak defaults
    fakeHDRIntensity = DVS4;
    saturationExpansionIntensity = DVS5;
#endif
    o0.rgb = FakeHDR(o0.rgb, normalizationPoint, fakeHDRIntensity, saturationExpansionIntensity);
  }
#endif

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  if (forceVanillaSDR)
  {
    o0.xyz = saturate(o0.xyz);
  }
  else if (LumaSettings.DisplayMode == 1) // Luma HDR
  {
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    settings.Mirrored = true; // Gamut map negatives too (there might be some!)
    settings.DesaturationVsDarkeningRatio = 0.333; // Some stuff can suddenly turn white if we do full desaturation
    o0.xyz = DICETonemap(o0.xyz * paperWhite, peakWhite, settings) / paperWhite;
  }
  else // Luma SDR
  {
#if 1
    o0.xyz = RestoreLuminance(o0.xyz, Reinhard::ReinhardRange(GetLuminance(o0.xyz), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
    o0.xyz = CorrectOutOfRangeColor(o0.xyz, true, true, 0.5, peakWhite / paperWhite); // TM by luminance generates out of gamut colors, and they were also already in the scene from grading
#else
    o0.xyz = Reinhard::ReinhardRange(o0.xyz, MidGray, -1.0, peakWhite / paperWhite, false);
#endif
  }

  // This won't hurt (the game was all drawn in gamma space with random operations!)
  FixColorGradingLUTNegativeLuminance(o0.rgb);
  
#if UI_DRAW_TYPE == 2
  o0.xyz *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2
  
  if (!gammaSpace)
  {
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
  }
}