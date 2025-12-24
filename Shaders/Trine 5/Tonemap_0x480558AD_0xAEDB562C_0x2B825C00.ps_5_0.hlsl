#define LUT_3D 1

#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"

Texture2D<float4> sceneTexture : register(t6); // HDR scene (non bloomed)
Texture3D<float4> lutTexture : register(t5); // 3d LUT (UNORM 16)
Texture2D<float4> bloomTexture : register(t4); // HDR bloomed scene
Texture2D<float4> overlayTexture : register(t2); // Unknown, some fullscreen overlay

SamplerState s2_s : register(s2); // LUT sampler (probably linear)
SamplerState s1_s : register(s1); // Scene sampler (either bilinear or nearest neighbor)

cbuffer cb0 : register(b0)
{
#if _AEDB562C
  float4 cb0[18];
#elif _480558AD
  float4 cb0[8];
#else // _2B825C00
  float4 cb0[6];
#endif
}

// 0 - SDR: Vanilla
// 1 - HDR: Vanilla+ (native method)
// 2 - HDR: Vanilla+ (inverse method)
// 3 - HDR: Untonemapped
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// Can generate more HDR colors (hues beyond Rec.709) and actually seems to hue shift less than tonemapping in sRGB for HDR too,
// probably because so many colors were at the edge of the gamut
#ifndef TONEMAP_IN_WIDER_GAMUT
#define TONEMAP_IN_WIDER_GAMUT 1
#endif

#ifndef FIX_LUT_SAMPLING
#define FIX_LUT_SAMPLING 1
#endif

// Some of the parameters that Trine 5 tonemapper had seem to create curves that have an early peak and then go down again,
// which not only can look broken, but also break display peak brightness mapping in HDR (because we can't predict the peak with them)
#ifndef FIX_BAD_TONEMAP_PARAMETERS
#define FIX_BAD_TONEMAP_PARAMETERS 1
#endif

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

float InverseLottesPeakTM(float peak, float exponent, float divisorAddend)
{
  return pow(-(peak - 1.0) / (peak * divisorAddend), -1.0 / exponent);
}

float InverseLottesPeakTM(float peak, float exponent, float divisorMultiplier, float divisorAddend)
{
  return pow(-((peak * divisorMultiplier) - 1.0) / (peak * divisorAddend), -1.0 / exponent);
}

// Broken
float InverseLottesPeakTM(float peak, float exponent, float divisorExponent, float divisorMultiplier, float divisorAddend)
{
  return log(((pow(peak, exponent) / divisorExponent) - divisorAddend) / divisorMultiplier) / (exponent * log(peak));
}

// https://www.desmos.com/calculator/8tfqttkvip
float LottesPeakTM(float peak, float exponent, float divisorExponent, float divisorMultiplier, float divisorAddend)
{
    return pow(peak, exponent) / (pow(peak, exponent * divisorExponent) * divisorMultiplier + divisorAddend);
}

// We found these heuristically, they are balanced to match the vanilla look
static const float lutSamplingFixNearBlackCorrection = 0.125;
static const float hdrSdrPeakRestorationPow = 1.333; // Old parameter, looks best at values near neutral (1), up until 2.5. Increase to lower the highlights intensity. Needs to be >= 1

void main(
  linear noperspective float2 vUV : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 outColor : SV_Target0)
{
  outColor.a = 1;

  const float hdrHighlightsStrength = LumaSettings.GameSettings.HDRHighlights; // Good values are between 0.25 and 0.3 (higher means weaker highlights)
  const float hdrDesaturation = LumaSettings.GameSettings.HDRDesaturation; // 1 for SDR like highlights hues (it feels too desaturated in HDR, it looks like AutoHDR), 0 for extremely deep fried saturated colors. Good range 0.7-1

  float3 bloomHDRColor = bloomTexture.SampleLevel(s1_s, vUV.xy, 0).rgb;
  float3 overlayColor = overlayTexture.Sample(s1_s, vUV.xy).rgb; // Whatever overlay (for some reason it's added before bloom)
  bloomHDRColor += overlayColor;
  float3 rawHDRColor = sceneTexture.Sample(s1_s, vUV.xy).rgb;

  float3 blendedHDRColor = lerp(rawHDRColor, bloomHDRColor, saturate(cb0[0].z)); // Bloom intensity (usually something between 0 and 1)
  // Tonemap to SDR and do grading in SDR (in linear space)
  // There were no negative (HDR) colors, so nothing gets lost in this, but it avoids all glitches with pow etc
#if DEVELOPMENT
  if (any(blendedHDRColor < -FLT_MIN))
  {
    outColor.rgb = float3(1, 0, 1);
    return;
  }
#endif
  blendedHDRColor = max(blendedHDRColor, 0.0);

#if _AEDB562C || _480558AD // LUT
  float3 tonemappedSDRColor = blendedHDRColor.rgb / (blendedHDRColor.rgb + 1.0); // Basic Reinhard
  float3 lutCoordinates = tonemappedSDRColor;

#if DRAW_LUT
  {
    bool drawnLUT = false;
    float3 LUTColor = DrawLUTTexture(lutTexture, ssHdrLinearClamp, v1.xy, drawnLUT);
    if (drawnLUT)
    {
      const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
      LUTColor *= paperWhite;
      outColor.rgba = float4(LUTColor, 0);
      return;
    }
  }
#endif // DRAW_LUT

#if FIX_LUT_SAMPLING // Fix LUT missing half texel bias (this causes crushed and clipped shadow and highlights)
	uint lutWidth;
	uint lutHeight;
	uint lutDepth;
	lutTexture.GetDimensions(lutWidth, lutHeight, lutDepth);
  float3 lutScale = (float3(lutWidth, lutHeight, lutDepth) - 1.0) / float3(lutWidth, lutHeight, lutDepth); // All dimensions are the same (64x)
  float3 lutBias = 0.5 / float3(lutWidth, lutHeight, lutDepth);
#if 1 // Correct near black level to maintain the original contrast, but without clipping (0.5/64 was clipped at the bottom and peak respectively) (we correct it up until the perceptual mid gray level)
  tonemappedSDRColor *= lerp(0.0, 1.0, pow(saturate(tonemappedSDRColor / gamma_to_linear1(0.5)), lutSamplingFixNearBlackCorrection));
#endif
  lutCoordinates = saturate(tonemappedSDRColor) * lutScale + lutBias;
#endif

  float3 gradedSDRColor = lutTexture.Sample(s2_s, lutCoordinates).rgb; // Linear 3D "SDR" LUT (it's large enough to be fine in linear space, 64x) (they don't seem to have raised blacks, so we don't normalize them)
  float3 gradedHDRColor = gradedSDRColor / max(9.99999975e-006, (1.0 - gradedSDRColor)); // Inverse Reinhard
  blendedHDRColor = lerp(blendedHDRColor.rgb, gradedHDRColor, cb0[7].x); // Grading intensity (usually 1)
#if 0 // Test LUT black level
  outColor.rgb = lutTexture.Sample(s2_s, 0).rgb;
  return;
#endif
#endif // _AEDB562C || _480558AD

  blendedHDRColor *= cb0[5].xyz; // HDR exposure multiplier (possibly to do fades to black, or clipping to ~white)

#if TONEMAP_TYPE >= 3
  outColor.rgb = /*sqrt*/(max(blendedHDRColor / 2.5, 0.0));
  return;
#endif

#if TONEMAP_IN_WIDER_GAMUT && TONEMAP_TYPE > 0
  if (LumaSettings.DisplayMode == 1)
    blendedHDRColor = BT709_To_BT2020(blendedHDRColor);
#endif

  // AMD Lottes tonemapper
  float lottesExponent = cb0[1].x;
  float lottesDivisorExponent = cb0[1].y;
  float lottesDivisorMultiplier = cb0[1].z;
  float lottesDivisorAddend = cb0[1].w;
  float colorPeakChannel = max(blendedHDRColor.r, max(blendedHDRColor.g, blendedHDRColor.b)); // Channels peak
  colorPeakChannel = max(5.96046448e-008, colorPeakChannel); //TODO: fix unnecessary loss (and in inverse reinhard above too!)
  float3 colorPeakRatio = blendedHDRColor / colorPeakChannel; // Color in the 0-1 range
  colorPeakRatio = pow(colorPeakRatio, cb0[2].xyz); // Enter ratio modulation space
  // "Advanced" Reinhard on the color peak. the y and z cb parameters are like 1 or close to it,
  // otherwise the results are borked, as if y is >1 the curve doesn't peak at 1, and goes back lower after an early peak,
  // though y could be lower than 1 to compress highlights, and z be smaller than one to cause early clipping, or bigger than one to compress the range.
  float tonemappedColorPeakChannel = LottesPeakTM(colorPeakChannel, lottesExponent, lottesDivisorExponent, lottesDivisorMultiplier, lottesDivisorAddend);
#if FIX_BAD_TONEMAP_PARAMETERS && TONEMAP_TYPE != 0 //TODO: re-enable these as they were?
  lottesDivisorExponent = lerp(lottesDivisorExponent, 1.0, 0.5); // Balance found heuristically
  lottesDivisorMultiplier = 1.0;
#endif
  float tonemappedSDRColorPeakChannel = saturate(tonemappedColorPeakChannel); // If we don't saturate, we risk flipping the hues, as if the peak is beyond 1, it's going to go
  float desaturationMul = 1.0;
#if TONEMAP_TYPE == 2 // Enable inverted TM HDR
  float outMidGray = MidGray;
  float inMidGray = MidGray;
  // In this case, we can invert the formula, otherwise, it's not invertible, and thus we need to use an approximation.
  // The image might snap a bit if the game dynamically changes these parameters.
  // "z" is seemengly 1 too, but due to floating point error accumulation it ends up with a tiny difference from it.
  if (abs(lottesDivisorExponent - 1.0) <= 0.001)
  {
    inMidGray = InverseLottesPeakTM(outMidGray, lottesExponent, lottesDivisorMultiplier, lottesDivisorAddend);
  }
  else
  {
    inMidGray = 0.3; // Output matches ~0.18 with the current set of parameters we had
    outMidGray = LottesPeakTM(inMidGray, lottesExponent, lottesDivisorExponent, lottesDivisorMultiplier, lottesDivisorAddend);
  }

  colorPeakChannel *= outMidGray / inMidGray;

  if (LumaSettings.DisplayMode == 1)
  {
    DICESettings settings = DefaultDICESettings();
    settings.Type = DICE_TYPE_BY_LUMINANCE_PQ; // DICE_TYPE_BY_LUMINANCE_RGB
    settings.ShoulderStart = 0.5;
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    colorPeakChannel = DICETonemap(colorPeakChannel * paperWhite, peakWhite, settings).x / paperWhite;
  }

  tonemappedColorPeakChannel = lerp(tonemappedColorPeakChannel, colorPeakChannel, saturate(pow(tonemappedColorPeakChannel / outMidGray, hdrSdrPeakRestorationPow))); // This doesn't extract much peak brightnes unfortunately
  desaturationMul = hdrDesaturation;
#elif TONEMAP_TYPE == 1 // Enable native HDR, using the same tonemapper but rebalanced to the HDR peak and paper white
  float dynamicRangeInv = LumaSettings.GamePaperWhiteNits / LumaSettings.PeakWhiteNits; // 1 for SDR, <1 for HDR
  float tonemappedHDRColorPeakChannel = LottesPeakTM(colorPeakChannel, lottesExponent, lottesDivisorExponent, lottesDivisorMultiplier * dynamicRangeInv, lottesDivisorAddend / lerp(dynamicRangeInv, 1.0, hdrHighlightsStrength));
  tonemappedColorPeakChannel = lerp(tonemappedColorPeakChannel, tonemappedHDRColorPeakChannel, saturate(pow(tonemappedColorPeakChannel, hdrSdrPeakRestorationPow))); // Restore SDR gradually up until 1, otherwise mid gray changes too much (in both directions, depending on how we modulate the settings), and HDR could be blinding
  // We need to desaturate based on the original SDR tonemapped peak, as that one would have been in a 0-1 range.
  // Even if we try to remap the new HDR "tonemappedColorPeakChannel" into 0-1, it would be heavily unbalanced, either desaturating too much or too little.
  // Given how much this game relies on desaturation, we can't afford to shift its results.
  desaturationMul = hdrDesaturation;
#endif
  // The higher the color is to the color peak channel, the less we add back some additive color (hence we don't clip highlights, though this would raise blacks) (this saturates highlights)
#if 1
  colorPeakRatio += pow(tonemappedSDRColorPeakChannel, cb0[3].xyz) * saturate(1.0 - colorPeakRatio) * desaturationMul;
#else // Alternative version. This isn't balanced as nicely across the range, it chagnes the desaturation of on stuff too much and on other too little
  colorPeakRatio += pow(pow(tonemappedSDRColorPeakChannel, cb0[3].xyz) * saturate(1.0 - colorPeakRatio), 1.0 / (lerp(desaturationMul, 1.0, 0.333)));
#endif
  colorPeakRatio = pow(colorPeakRatio, cb0[4].xyz); // Exit ratio modulation space

  float3 finalColor = colorPeakRatio * tonemappedColorPeakChannel; // Denormalize color
  
#if TONEMAP_IN_WIDER_GAMUT && TONEMAP_TYPE > 0
  if (LumaSettings.DisplayMode == 1)
    finalColor = BT2020_To_BT709(finalColor);
#endif

#if _AEDB562C && ENABLE_VIGNETTE
  float4 r0,r1;
  r1.xyz = finalColor + cb0[16].yzw;
  r1.xyz = -finalColor * cb0[16].yzw + r1.xyz;
  r1.xyz = cb0[16].x * r1.xyz + (cb0[16].yzw * finalColor);
  r0.xyz = -finalColor + r1.xyz;
  float aspectRatio = (float)asuint(cb0[0].x) / (float)asuint(cb0[0].y);
  float2 centeredUV = vUV.xy - 0.5; // Offset texture coordinates from 0|1 to -0.5|0.5
  centeredUV.x *= aspectRatio;
  float vignetteIntensity = length(centeredUV);
  vignetteIntensity = saturate(vignetteIntensity / cb0[17].y);
  vignetteIntensity = sqr(vignetteIntensity) * (vignetteIntensity * -2 + 3) * cb0[17].x;
  finalColor = vignetteIntensity * r0.xyz + finalColor;
#endif

#if 1 // Luma: output linear directly, given we adjusted the sharpening/TAA shaders to take a linear color
  outColor.rgb = finalColor;
#else // Sharpening/TAA output (gamma 2.0, which would then be turned to sRGB by TAA after sharpening)
  outColor.rgb = sqrt(max(finalColor, 0.0)); // AA shader does x*x as square so we need to clip negative values
#endif
}