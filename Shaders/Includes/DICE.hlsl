#ifndef SRC_DICE_HLSL
#define SRC_DICE_HLSL

#include "Common.hlsl"
#include "ColorGradingLUT.hlsl"

namespace DICE
{
  // Applies exponential ("Photographic") luminance/luma compression.
  // The max is the max possible range to compress from, to not lose any output range if the input range was limited.
  float rangeCompress(float X, float Max = FLT_MAX)
  {
    // Branches are for static parameters optimizations
    if (Max == FLT_MAX)
    {
      // This does e^X. We expect X to be between 0 and 1.
      return 1.f - exp(-X);
    }
    const float lostRange = exp(-Max);
    const float restoreRangeScale = 1.f / (1.f - lostRange);
	  float compression = 1.f - exp(-X);
    return lerp(compression, compression * restoreRangeScale, compression);
  }

  // Refurbished DICE HDR tonemapper (per channel or luminance).
  // Expects "InValue" to be >= "ShoulderStart" and "OutMaxValue" to be > "ShoulderStart".
  float luminanceCompress(
    float InValue,
    float OutMaxValue,
    float ShoulderStart = 0.f,
    bool ConsiderMaxValue = false,
    float InMaxValue = FLT_MAX)
  {
    const float compressableValue = InValue - ShoulderStart;
    const float compressableRange = InMaxValue - ShoulderStart;
    const float compressedRange = OutMaxValue - ShoulderStart;
    const float possibleOutValue = ShoulderStart + compressedRange * rangeCompress(compressableValue / compressedRange, ConsiderMaxValue ? (compressableRange / compressedRange) : FLT_MAX);
#if 1
    return possibleOutValue;
#else // Enable this branch if "InValue" can be smaller than "ShoulderStart"
    return (InValue <= ShoulderStart) ? InValue : possibleOutValue;
#endif
  }
}

// Compresses by luminance in rgb linear space. Highlights compression is too weak and stuff clips.
#define DICE_TYPE_BY_LUMINANCE_RGB 0
// Doing the DICE compression in PQ (either on luminance or each color channel) produces a curve that is closer to our "perception" and leaves more detail highlights without overly compressing them
#define DICE_TYPE_BY_LUMINANCE_PQ 1
// Modern HDR displays clip individual rgb channels beyond their "white" peak brightness,
// like, if the peak brightness is 700 nits, any r g b color beyond a value of 700/80 will be clipped (not acknowledged, it won't make a difference).
// Tonemapping by luminance, is generally more perception accurate but can then generate rgb colors "out of range". This setting fixes them up,
// though it's optional as it's working based on assumptions on how current displays work, which might not be true anymore in the future.
// Note that this can create some steep (rough, quickly changing) gradients on very bright colors.
#define DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 2
// Same as "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" but before that, it also calculates a tonemap by channel
// and restores the chrominance of that on the luminance tonemap, this simultaneously avoids hue shifts,
// while keeping a nice highlights rolloff (desaturation), that looks natural.
// The correction is still useful to avoid hue shifts from channels that go outside the display range.
#define DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 3
// This might look more like classic SDR tonemappers and is closer to how modern TVs and Monitors play back colors (usually they clip each individual channel to the peak brightness value, though in their native panel color space, or current SDR/HDR mode color space).
// Overall, this seems to handle bright gradients more smoothly, even if it shifts hues more (and generally desaturating).
#define DICE_TYPE_BY_CHANNEL_PQ 4
// TODO: add perceptual log version? It doesn't look much better than PQ, though it should be simpler in the end (faster) (https://www.desmos.com/calculator/886c46d2ef). Also try using the max to clip the input at at certain level instead of remapping it from infinite.
// TODO: split these into different settings, given almost all combinations are possible

struct DICESettings
{
  uint Type;
  // Determines where the highlights curve (shoulder) starts.
  // Values between 0.25 and 0.5 are good with DICE by PQ (any type).
  // It automatically scales by your peak in PQ DICE, though you might want to increase it in SDR to clip more, instead of keeping a large range for soft highlights.
  // With linear/rgb DICE this barely makes a difference, zero is a good default but (e.g.) 0.5 would also work.
  // This should always be between 0 and 1.
  float ShoulderStart;

  // Tonemap negative colors as well. It might better compress out of gamut colors.
  bool Mirrored;

  // For "Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" and "DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" only:
  // The sum of these needs to be <= 1, both within 0 and 1.
  // The closer the sum is to 1, the more each color channel will be contained within its peak range.
  float DesaturationAmount;
  float DarkeningAmount;
};

DICESettings DefaultDICESettings(uint Type = DICE_TYPE_BY_CHANNEL_PQ)
{
  DICESettings Settings;
  Settings.Type = Type;
  Settings.ShoulderStart = (Settings.Type > DICE_TYPE_BY_LUMINANCE_RGB) ? (1.0 / 3.0) : 0.0; // Setting it higher than 1/3 might cause highlights clipping as detail is too compressed. Setting it lower than 1/4 would probably look dynamic range. 1/3 seems like the best compromize. There's usually no need to start compressing from paper white just to keep the SDR range unchanged.
  Settings.Mirrored = false;
  Settings.DesaturationAmount = 0.5;
  Settings.DarkeningAmount = 0.5;
  return Settings;
}

// Tonemapper inspired from DICE. Can work by luminance to maintain hue.
// Takes scRGB colors with a white level (the value of 1 1 1) of 80 nits (sRGB) (to not be confused with paper white).
// Paper white is expected to have already been multiplied in the color.
// This currently often works in BT.2020 so it's not suggested for SDR usage.
float3 DICETonemap(
  float3 Color,
  float PeakWhite,
  const DICESettings Settings /*= DefaultDICESettings()*/)
{
  const float sourceLuminance = GetLuminance(Color);

  if (Settings.Type != DICE_TYPE_BY_LUMINANCE_RGB)
  {
    static const float HDR10_MaxWhite = HDR10_MaxWhiteNits / sRGB_WhiteLevelNits;

    // We could first convert the peak white to PQ and then apply the "shoulder start" alpha to it (in PQ),
    // but tests showed scaling it in linear actually produces a better curve and more consistently follows the peak across different values
    const float shoulderStartPQ = Linear_to_PQ((Settings.ShoulderStart * PeakWhite) / HDR10_MaxWhite).x;
    if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
    {
      const float sourceLuminanceNormalized = sourceLuminance / HDR10_MaxWhite;
      const float sourceLuminancePQ = Linear_to_PQ(sourceLuminanceNormalized, GCT_POSITIVE).x;

      if (sourceLuminancePQ > shoulderStartPQ) // Luminance below the shoulder (or below zero) don't need to be adjusted
      {
        const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

        bool BT2020 = Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE;

        if (BT2020)
        {
          Color = BT709_To_BT2020(Color);
        }
        const float3 originalColor = Color;

        const float compressedLuminancePQ = DICE::luminanceCompress(sourceLuminancePQ, peakWhitePQ, shoulderStartPQ);
        const float compressedLuminanceNormalized = PQ_to_Linear(compressedLuminancePQ).x;
        Color *= compressedLuminanceNormalized / sourceLuminanceNormalized;

        if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
        {
          float3 perChannelTMColor = originalColor;
          const float3 sourceColorNormalized = perChannelTMColor / HDR10_MaxWhite;
          const float3 sourceColorPQ = Linear_to_PQ(sourceColorNormalized, GCT_POSITIVE);
          [unroll]
          for (uint i = 0; i < 3; i++)
          {
            if (sourceColorPQ[i] > shoulderStartPQ)
            {
              const float compressedColorPQ = DICE::luminanceCompress(sourceColorPQ[i], peakWhitePQ, shoulderStartPQ);
              const float compressedColorNormalized = PQ_to_Linear(compressedColorPQ).x;
              perChannelTMColor[i] *= compressedColorNormalized / sourceColorNormalized[i];
            }
          }
          Color = RestoreHueAndChrominance(Color, perChannelTMColor, 0.0, Settings.DesaturationAmount, 0.0, FLT_MAX, 0.0, BT2020 ? CS_BT2020 : CS_BT709); // TODO: try with ICtCp or UCS etc. Also expose the min/max chrominance changes (however it should almost always decrease against by channel TM)
        }

        if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
        {
          Color = CorrectOutOfRangeColor(BT2020 ? Color : BT709_To_BT2020(Color), false, true, Settings.DesaturationAmount, Settings.DarkeningAmount, PeakWhite, CS_BT2020); // TODO: review this code, does it work as expected for all input settings? Maybe not (see in Heavy Rain, the first police scene)
          BT2020 = true;
        }

        if (BT2020)
        {
          Color = BT2020_To_BT709(Color);
        }
      }
    }
    else // DICE_TYPE_BY_CHANNEL_PQ
    {
      const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

      // Tonemap in BT.2020 to more closely match the primaries (and range) of modern displays
      Color = BT709_To_BT2020(Color);
      float3 sourceColorNormalized = Color / HDR10_MaxWhite;
      if (Settings.Mirrored) // TODO: implement "Mirrored" in other DICE types
        sourceColorNormalized = abs(sourceColorNormalized);
      const float3 sourceColorPQ = Linear_to_PQ(sourceColorNormalized, GCT_POSITIVE);

      [unroll]
      for (uint i = 0; i < 3; i++) //TODO LUMA: optimize? will the shader compile already convert this to float3? Or should we already make a version with no branches that works in float3?
      {
        if (sourceColorPQ[i] > shoulderStartPQ) // Colors below the shoulder (or below zero) don't need to be adjusted
        {
          const float compressedColorPQ = DICE::luminanceCompress(sourceColorPQ[i], peakWhitePQ, shoulderStartPQ);
          const float compressedColorNormalized = PQ_to_Linear(compressedColorPQ).x;
          Color[i] *= compressedColorNormalized / sourceColorNormalized[i];
        }
      }
      
      Color = BT2020_To_BT709(Color);
    }
  }
  else // DICE_TYPE_BY_LUMINANCE_RGB
  {
    const float shoulderStart = Settings.ShoulderStart * PeakWhite; // From alpha to linear range
    if (sourceLuminance > shoulderStart) // Luminances below the shoulder (or below zero) don't need to be adjusted
    {
      const float compressedLuminance = DICE::luminanceCompress(sourceLuminance, PeakWhite, shoulderStart);
      Color *= compressedLuminance / sourceLuminance;
    }
  }

  return Color;
}

#endif // SRC_DICE_HLSL