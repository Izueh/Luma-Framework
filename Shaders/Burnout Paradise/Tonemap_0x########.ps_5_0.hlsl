#define LUT_3D 1

#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#if _382FAE1E || _4933D9DA || _7E095B44 || _9613B478 || _A4AAD10C || _BE5A4C0C || _BF6A19BE || _D48AAAA6
#define MOTION_BLUR_QUALITY 2
#elif _259C7CD1 || _36F104E3 || _5A34E415 || _6B63F6E9 || _959BB01D || _A47D890F || _C0305DC5 || _D29A0825
#define MOTION_BLUR_QUALITY 1
#endif

#if _10807557 || _382FAE1E || _4933D9DA || _57B58AF1 || _6B63F6E9 || _959BB01D || _B9F09845 || _BE5A4C0C || _BF6A19BE || _C0305DC5 || _D29A0825 || _D772072E
#define ENABLE_DOF 1
#endif

#if _259C7CD1 || _5A34E415 || _6B63F6E9 || _959BB01D || _A4AAD10C || _B9F09845 || _BE5A4C0C || _BF6A19BE || _C0BD8148 || _D48AAAA6 || _D772072E || _FDE602F4
#define ENABLE_SSAO 1
#endif

#ifndef ENABLE_COLOR_GRADING_LUT // Can be overriden by users
#if _4933D9DA || _57B58AF1 || _5A34E415 || _7E095B44 || _959BB01D || _A47D890F || _BF6A19BE || _D29A0825 || _D48AAAA6 || _D772072E || _DD905507 || _FDE602F4
#define ENABLE_COLOR_GRADING_LUT 1
#endif
#endif

// Default settings (all off), given that we only specify which permutations use each feature.
// There's 24 combinations of all of these.
// Permutation "07297021" is void of all optional features.
#ifndef MOTION_BLUR_QUALITY
// 0: Disabled
// 1: Medium
// 2: High
#define MOTION_BLUR_QUALITY 0
#endif
#ifndef ENABLE_COLOR_GRADING_LUT
#define ENABLE_COLOR_GRADING_LUT 0
#endif
#ifndef ENABLE_DOF
#define ENABLE_DOF 0
#endif
#ifndef ENABLE_SSAO
#define ENABLE_SSAO 0
#endif

cbuffer _Globals : register(b0)
{
  float4 GlobalParams : packoffset(c0);
  float4 DofParamsA : packoffset(c1);
  float4 DofParamsB : packoffset(c2);
  float4 BloomColour : packoffset(c3);
  float4 VignetteInnerRgbPlusMul : packoffset(c4);
  float4 VignetteOuterRgbPlusAdd : packoffset(c5);
  float4 Tint2dColour : packoffset(c6);
  float4 BlurMatrixZZZ : packoffset(c7);
  float4 MotionBlurStencilValues : packoffset(c8);
  float4 AdaptiveLuminanceValues : packoffset(c9);
}

SamplerState SamplerSource_s : register(s0);
SamplerState SamplerBloom_s : register(s1);
SamplerState SamplerDof_s : register(s2);
SamplerState Sampler3dTint_s : register(s3);
SamplerState SamplerDepth_s : register(s4);
SamplerState SamplerSSAO_s : register(s6);
SamplerState SamplerParticles_s : register(s7);
SamplerState samplerLastAvgLuminance_s : register(s9);

Texture2D<float4> SamplerSourceTexture : register(t0);
Texture2D<float4> SamplerBloomTexture : register(t1);
Texture2D<float4> SamplerDofTexture : register(t2);
Texture3D<float4> Sampler3dTintTexture : register(t3);
Texture2D<float> SamplerDepthTexture : register(t4);
Texture2D<float> SamplerSSAOTexture : register(t6);
Texture2D<float4> SamplerParticlesTexture : register(t7);
Texture2D<float2> samplerLastAvgLuminanceTexture : register(t9);

float3 ApplyLUT(float3 color, float3 sdrColor, Texture3D<float4> _texture, SamplerState _sampler)
{
  float vanillaCompressionRatio = max(max3(sdrColor), 1.0);

  bool lutExtrapolation = true;
  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = color;
  extrapolationData.vanillaInputColor = sdrColor / vanillaCompressionRatio; // Nicely compress back to 1 to fit the LUT
  
  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
#if DEVELOPMENT // TODO: make sure there's no hue shifts (it seems ok for the most part, and we now have "LumaSettings.GameSettings.OriginalTonemapperColorIntensity" anyway)
  extrapolationSettings.enableExtrapolation = DVS1 <= 0.0;
#endif
  //extrapolationSettings.lutSize = 32; // It probably doesn't ever change but let it be automatically determined
  extrapolationSettings.inputLinear = true;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = false;
  extrapolationSettings.outputLinear = true;
  extrapolationSettings.vanillaLUTRestorationAmount = LumaSettings.GameSettings.OriginalTonemapperColorIntensity; // Not a 100% match!
#if 1 // High quality. Not particularly needed in this game as most LUTs are neutral, but it won't hurt.
  extrapolationSettings.extrapolationQuality = 2;
#endif
  
  color = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);

  color *= lerp(1.0, vanillaCompressionRatio, LumaSettings.GameSettings.OriginalTonemapperColorIntensity);

  return color;
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
#if MOTION_BLUR_QUALITY > 0
  float3 v2 : TEXCOORD1,
#endif
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  bool forceVanilla = ShouldForceSDR(v1.xy);

  float3 bloomTextureColor = SamplerBloomTexture.Sample(SamplerBloom_s, v1.xy).xyz;

  float invAmbientOcclusion = 1.0;
  float paritclesAlpha = 0.0;
#if ENABLE_SSAO
  invAmbientOcclusion = SamplerSSAOTexture.Sample(SamplerSSAO_s, v1.xy).x;
  paritclesAlpha = SamplerParticlesTexture.Sample(SamplerParticles_s, v1.xy).w;
#if 1 // Luma: make sure particles alpha isn't beyond range due to texture upgrades
  paritclesAlpha = saturate(paritclesAlpha);
#endif
#endif

  float3 dofTextureColor = 0.0;
  float depth = 0.0;
#if ENABLE_DOF || MOTION_BLUR_QUALITY > 0
  dofTextureColor = SamplerDofTexture.Sample(SamplerDof_s, v1.xy).xyz;
  depth = SamplerDepthTexture.Sample(SamplerDepth_s, v1.xy).x;
#endif

  if (forceVanilla)
  {
    bloomTextureColor = saturate(bloomTextureColor);
    dofTextureColor = saturate(dofTextureColor);
  }

  // Motion Blur
#if MOTION_BLUR_QUALITY > 0

#if ENABLE_IMPROVED_MOTION_BLUR // TODO
  const int iterations = 32;
#elif MOTION_BLUR_QUALITY >= 2
  const int iterations = 16;
#else // MOTION_BLUR_QUALITY <= 1
  const int iterations = 4;
#endif // MOTION_BLUR_QUALITY >= 2
  float sceneAlpha = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).w; // This is a mask that represents different types of objects (like a stencil)
  float motionBlurStencilValue = (sceneAlpha < 0.7) ? MotionBlurStencilValues.y : MotionBlurStencilValues.x;
  r3.xyz = BlurMatrixZZZ.xyz * depth + v2.xyz;
  r3.xy = r3.xy + r3.z * v1.xy;
  r3.xy *= motionBlurStencilValue * LumaSettings.GameSettings.MotionBlurIntensity;
  r3.zw = r3.xy / float(iterations);
  float2 motionBlurUV = r3.zw * frac(v0.x * 0.618034005 + v0.y * v0.y * 0.381966025) + v1.xy; // It's not clear why only the first iteration is scaled in UV
  float3 tempSceneColorSum = 0.0;
  int r0iw = 0;
  while (true)
  {
    if (r0iw >= iterations) break;

#if ENABLE_SSAO && ENABLE_IMPROVED_MOTION_BLUR // Luma: particles here weren't sampled in the right place
    invAmbientOcclusion = SamplerSSAOTexture.Sample(SamplerSource_s, motionBlurUV).x;
    paritclesAlpha = SamplerParticlesTexture.Sample(SamplerSource_s, motionBlurUV).w;
    paritclesAlpha = saturate(paritclesAlpha);
#endif
    float3 tempSceneTextureColor = SamplerSourceTexture.Sample(SamplerSource_s, motionBlurUV).xyz;
    if (forceVanilla)
    {
      tempSceneTextureColor = saturate(tempSceneTextureColor);
    }
    else
    {
      tempSceneTextureColor = max(tempSceneTextureColor, -FLT16_MAX); // Luma: NaNs protection
    }
    tempSceneColorSum += lerp(tempSceneTextureColor * invAmbientOcclusion, tempSceneTextureColor, paritclesAlpha); // Apply SSAO if not on particles
    
    motionBlurUV += r3.xy / float(iterations);

    r0iw++;
  }
  float3 composedColor = tempSceneColorSum / float(iterations);
  
#else // MOTION_BLUR_QUALITY <= 0

  float3 composedColor = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).xyz;
  if (forceVanilla)
  {
    composedColor = saturate(composedColor);
  }
  else
  {
    composedColor = max(composedColor, -FLT16_MAX); // Luma: NaNs protection
  }
  composedColor = lerp(composedColor * invAmbientOcclusion, composedColor, paritclesAlpha); // Apply SSAO if not on particles

#endif // MOTION_BLUR_QUALITY > 0

#if ENABLE_DOF
  // Depth of Field
  float dofAlpha = DofParamsB.x * saturate(max(DofParamsB.y * (DofParamsA.y - depth), DofParamsB.z * (-DofParamsA.z + depth)));
  composedColor = lerp(composedColor, dofTextureColor.xyz, dofAlpha); // Blend in DoF by distance etc (focal plane)
#endif // ENABLE_DOF

  // Scene exposure (hardcoded by time of day etc), used to keep visibility balanced
  // Note: bloom isn't affected by this so if this reduces the scene color, in contrast bloom will be stronger.
  composedColor *= GlobalParams.x;

#if 1 // This is some kind of auto exposure loop, based on the luminance of the previous frame (pre tonemapping)
  r2.xy = samplerLastAvgLuminanceTexture.Sample(samplerLastAvgLuminance_s, float2(0.5, 0.5)).xy; // TODO: test if HDR messes up the balance of this? It doesn't seem to at first glange. We could saturate the luminance generation otherwise, or here.
  r0.w = r2.x - r2.y;
  r1.w = (abs(r0.w) < AdaptiveLuminanceValues.w);
  r0.w = r1.w ? 0 : r0.w;
  r1.w = AdaptiveLuminanceValues.z - AdaptiveLuminanceValues.y;
  r0.w = r0.w / r1.w;
  r0.w = max(-1, r0.w);
  r0.w = min(1, r0.w);
  r0.w = AdaptiveLuminanceValues.x * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  composedColor += r0.w;
#endif

  float3 sdrComposedColor = composedColor;

  // Bloom
  float3 finalBloomColor = bloomTextureColor * BloomColour.xyz * LumaSettings.GameSettings.BloomIntensity;
#if 1 // Luma: fixed bloom going negative with HDR values
  if (!forceVanilla)
  {
    bool improvedBloom = false;
#if ENABLE_IMPROVED_BLOOM
    improvedBloom = LumaSettings.DisplayMode == 1;
#endif // ENABLE_IMPROVED_BLOOM
    if (improvedBloom) // Just add the bloom as it came, it looks nicer in HDR
    {
      composedColor += finalBloomColor;
    }
    else // Almost the same as the vanilla formula, but re-written to not for colors beyond 1
    {
      composedColor += finalBloomColor * (1.0 - saturate(composedColor));
    }
  }
  else
  {
    composedColor += finalBloomColor - saturate(finalBloomColor * composedColor);
  }
#else
  // This avoids adding bloom on highlights, preventing the image from clipping too much.
  composedColor += finalBloomColor - saturate(finalBloomColor * composedColor);
#endif
  // Note: theoretically this isn't the original bloom color, but it's actually slightly "better"
  sdrComposedColor += finalBloomColor * (1.0 - saturate(sdrComposedColor));

  // Color grading LUT
#if ENABLE_COLOR_GRADING_LUT
#if 1 // Luma
  if (!forceVanilla) // TODO: SDR is still overly bright compared to vanilla when it passes through here, see "adjustmentScale"
  {
    float clippedAmount = 0.5 / 32.0; // The first and last half texels of the LUT were clipped away (in gamma space)

    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
    sdrComposedColor = ((sdrComposedColor - 0.5) * (1.0 + clippedAmount)) + 0.5; // Emulate the SDR LUT error with LUT extrapolation
    sdrComposedColor = gamma_to_linear(sdrComposedColor, GCT_MIRROR);

    // TODO: put this in a formula given that it's identical to BS2
    // LUTs were clipped around the first and half texel due to bad sampling math. This will emulate the shadow darkening and highlight brightening from it (contrast boost), without causing clipping.
    // The original error applied in gamma space but we do it in linear.
#if LUT_SAMPLING_ERROR_EMULATION_MODE > 0
    float3 previousColor = composedColor.rgb;
    
    // Adjust params for shadows
    // Empyrically found values that look good
    float adjustmentScale = 0.15; // Basically the added contrast curve strength
    float adjustmentRange = 1.0 / 3.0; // Theoretically the added shadow crush would have happened until 0.5 (in gamma space, and ~0.218 in linear), by then it would have faded out (and highlights clipping would have begun, but we don't simulate that)
    float adjustmentPow = 1.0; // Values > 1 might look good too, this kinda controls the "smoothness" of the contrast curve
#if LUT_SAMPLING_ERROR_EMULATION_MODE != 2 // Per channel (it looks nicer)
    composedColor.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma(previousColor, GCT_POSITIVE) / adjustmentRange, adjustmentPow)));
#else // LUT_SAMPLING_ERROR_EMULATION_MODE == 2 // By luminance
    composedColor.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma1(max(GetLuminance(previousColor), 0.0)) / adjustmentRange, adjustmentPow)));
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 2

#if LUT_SAMPLING_ERROR_EMULATION_MODE != 3
    // Adjust params for highlights
    adjustmentScale = 1.0 - adjustmentScale; // Flip it because it looks good like this
    adjustmentRange = 1.0 - adjustmentRange; // Do the remaining range
    float3 highlightsPerChannelScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma(previousColor, GCT_SATURATE)) / adjustmentRange, adjustmentPow)));
    float highlightsByLuminanceScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma1(saturate(GetLuminance(previousColor)))) / adjustmentRange, adjustmentPow)));
#if 0 // Per channel (looks deep fried)
    composedColor.rgb *= highlightsPerChannelScale;
#elif 0 // By luminance (looks like AutoHDR)
    composedColor.rgb *= highlightsByLuminanceScale;
#else // Mixed (looks best on highlights)
    composedColor.rgb *= lerp(highlightsPerChannelScale, highlightsByLuminanceScale, LumaSettings.DisplayMode == 1 ? 0.75 : 0.333);
#endif
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 3
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE > 0

    float3 preLUTColor = composedColor;
    composedColor = ApplyLUT(composedColor, sdrComposedColor, Sampler3dTintTexture, Sampler3dTint_s); // Output is linear

    // TODO: turn this into a function given it's used by Mafia III as well? We could also try to get the neutral LUT color at each location and remove the filter it applied by calculating the rgb ratio of the lutted color against the luminance or something
    // Most of the blue is from vignette, not LUTs, these just add some contrast etc
    float3 lutMidGreyGamma = Sampler3dTintTexture.Sample(Sampler3dTint_s, 0.5).rgb;
    float3 lutMidGreyLinear = gamma_to_linear(lutMidGreyGamma, GCT_NONE); // Turn linear
    float lutMidGreyBrightnessLinear = max(GetLuminance(lutMidGreyLinear), 0.0); // Normalize it by luminance
    float blueCorrectionIntensity = LumaSettings.GameSettings.ColorGradingDebluingIntensity; // Note that this will correct other color filters as well!
    composedColor /= (lutMidGreyLinear != 0.0) ? lerp(1.0, safeDivision(lutMidGreyLinear, lutMidGreyBrightnessLinear, 1), blueCorrectionIntensity) : 1.0;

    composedColor = lerp(preLUTColor, composedColor, LumaSettings.GameSettings.ColorGradingIntensity);

    composedColor = linear_to_gamma(composedColor, GCT_MIRROR);
  }
  else
  {
    composedColor = Sampler3dTintTexture.Sample(Sampler3dTint_s, composedColor).xyz;
  }
#else // The original LUT sampling failed to acknowledge the half texel offset of LUTs and clipped colors
  composedColor = Sampler3dTintTexture.Sample(Sampler3dTint_s, composedColor).xyz;
#endif
#endif // ENABLE_COLOR_GRADING_LUT
  
  // Luma: HDR boost
  // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that. Do it before bloom to avoid bloom going crazy too
  if (!forceVanilla && LumaSettings.DisplayMode == 1)
  {
    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);

    float normalizationPoint = 0.05; // Found empyrically
    float fakeHDRIntensity = LumaSettings.GameSettings.HDRBoostIntensity * 0.333; // 0.1-0.15 looks good in most places. 0.2 looks better in dim scenes, but is too much AutoHDR like in bright scenes
    float fakeHDRSaturation = 0.5;
#if DEVELOPMENT // TODO: saturation, and balance with sky!!!
    fakeHDRSaturation = DVS4;
#endif
    composedColor = FakeHDR(composedColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    
    composedColor.xyz = linear_to_gamma(composedColor, GCT_MIRROR);
  }

#if ENABLE_VIGNETTE
  // Vignette (note that this is also affected by the user brightness calibration, acting as contrast modulator)
  // This is partially what added the blue tint too.
  float vignetteIntensity = saturate(VignetteOuterRgbPlusAdd.w + sqrt(dot(v1.zw, v1.zw))); // Calculate vignette based on the distance from the center of the screen. This seems to be UW friendly.
  vignetteIntensity = (vignetteIntensity * -2 + 3) * vignetteIntensity * vignetteIntensity;
  float3 vignetteInnerRgbPlusMul = VignetteInnerRgbPlusMul.xyz;
  float3 vignetteOuterRgbPlusAdd = VignetteOuterRgbPlusAdd.xyz;

#if 1 // Luma
  float vignetteColorIntensity = 1.0;
#if 1
  // Remove the color filter of the central vignette from the edges vignette,
  // this is because the center has some blue tint too, and that's "excessive" but the one at the edges is kinda done to emulate the sky being blue, and that we want to keep.
  // We leave the upper part (sky) blue, this makes sense in most shots as the camera is always behind the car
  if (v1.w < 0.5)
  {
    float3 vignetteCenterColorRatio = GetLuminance(vignetteInnerRgbPlusMul) / vignetteInnerRgbPlusMul;
    vignetteInnerRgbPlusMul *= lerp(1.0, vignetteCenterColorRatio, LumaSettings.GameSettings.ColorGradingDebluingIntensity);
    vignetteOuterRgbPlusAdd *= lerp(1.0, vignetteCenterColorRatio, LumaSettings.GameSettings.ColorGradingDebluingIntensity);
    vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity;
  }
  // Remove blue tint from "floor" too
  else
  {
    vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity * (1.0 - LumaSettings.GameSettings.ColorGradingDebluingIntensity);
  }
#else // This branch removes the blue from the sky (and floor) as well
  vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity * (1.0 - LumaSettings.GameSettings.ColorGradingDebluingIntensity);
#endif

  // Take away their color if we don't want any grading.
  // We restore the original luminance, even if these are gamma space multipliers and addends, however it still looks better than restoring their average.
  vignetteInnerRgbPlusMul = lerp(GetLuminance(vignetteInnerRgbPlusMul), vignetteInnerRgbPlusMul, vignetteColorIntensity);
  vignetteOuterRgbPlusAdd = lerp(GetLuminance(vignetteOuterRgbPlusAdd), vignetteOuterRgbPlusAdd, vignetteColorIntensity);
  
  // Find the brightness multiplication offset at the center of the screen and remove it from both mult factors if we are disabling color grading,
  // but default the game dimmed the image and "failed" to use the whole dynamic range.
  float3 vignetteCenterColorOffset = 1.0 - vignetteInnerRgbPlusMul;
  vignetteCenterColorOffset *= 1.0 - LumaSettings.GameSettings.ColorGradingIntensity;
  vignetteInnerRgbPlusMul += vignetteCenterColorOffset;
  vignetteOuterRgbPlusAdd += vignetteCenterColorOffset;
#endif

  // Blend between color mult at the center and color mult at the edges
  // Note: the multiplier "part" at the edges of the screen might be best applied after tonemapping with Luma, but whatever, this will work anyway!
  float3 vignetteMultiplier = lerp(vignetteInnerRgbPlusMul, vignetteOuterRgbPlusAdd, vignetteIntensity);
  composedColor *= vignetteMultiplier;
  //composedColor = vignetteMultiplier; // Test: view vignette color and intensity
#endif

  // Tint (by default this is 0 and matches the user brightness/contrast calibration)
  composedColor += Tint2dColour.xyz;
  
  // Luma: Tonemapping
  if (!forceVanilla)
  {
    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1; // Vanilla clipped (hue shifted) look is better preserved with this
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      composedColor = DICETonemap(composedColor * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
      float shoulderStart = 0.667; // Set it higher than "MidGray", otherwise it compresses too much.
      if (tonemapPerChannel)
      {
        composedColor = Reinhard::ReinhardRange(composedColor, shoulderStart, -1.0, peakWhite / paperWhite, false);
      }
      else
      {
        composedColor = RestoreLuminance(composedColor, Reinhard::ReinhardRange(GetLuminance(composedColor), shoulderStart, -1.0, peakWhite / paperWhite, false).x, true);
        composedColor = CorrectOutOfRangeColor(composedColor, true, true, 0.5, 0.5, peakWhite / paperWhite);
      }
    }

    composedColor.xyz = linear_to_gamma(composedColor, GCT_MIRROR);
  }

  o0.xyz = composedColor; 
  o0.w = 1;

#if UI_DRAW_TYPE == 2 // TODO: theoretically we should undo this transformation for the FXAA shader (hash 0xF4CB0620) that runs later, however FXAA doesn't look good in this game and MSAA 8x look better
  // Note that this shader is also used in the game's display calibration menu!
  const float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;
  o0.rgb /= pow(UIPaperWhite, 1.0 / DefaultGamma);
  o0.rgb *= pow(gamePaperWhite, 1.0 / DefaultGamma);
#endif
}