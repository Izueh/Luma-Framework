#define LUT_SIZE 16.0
#define LUT_MAX (LUT_SIZE - 1.0)

#define GCT_DEFAULT GCT_MIRROR

#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer _Globals : register(b0)
{
  float4 SceneShadowsAndDesaturation : packoffset(c0);
  float4 SceneInverseHighLights : packoffset(c1);
  float4 SceneMidTones : packoffset(c2);
  float4 SceneScaledLuminanceWeights : packoffset(c3);
  float4 GammaColorScaleAndInverse : packoffset(c4);
  float4 GammaOverlayColor : packoffset(c5);
  float4 RenderTargetExtent : packoffset(c6);
  float2 DownsampledDepthScale : packoffset(c7);
  float4 ImageAdjustment : packoffset(c8);
}

SamplerState SceneColorTexture_s : register(s0);
SamplerState DOFAndMotionBlurImage_s : register(s1);
SamplerState DOFAndMotionBlurInfoImage_s : register(s2);
SamplerState BloomOnlyImage_s : register(s3);
SamplerState LightShaftTexture_s : register(s4);
SamplerState ColorGradingLUT_s : register(s5);
Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float4> DOFAndMotionBlurImage : register(t1);
Texture2D<float4> DOFAndMotionBlurInfoImage : register(t2);
Texture2D<float4> BloomOnlyImage : register(t3);
Texture2D<float4> LightShaftTexture : register(t4); // Note: this texture is SDR (UNORM)
Texture2D<float4> ColorGradingLUT : register(t5);

#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

#ifndef ENABLE_FAKE_HDR
#define ENABLE_FAKE_HDR 1
#endif

#ifndef TONEMAP_IN_WIDER_GAMUT
#define TONEMAP_IN_WIDER_GAMUT 1
#endif

#ifndef FIX_SDR_TONEMAPPER_TYPE
#define FIX_SDR_TONEMAPPER_TYPE 1
#endif

#ifndef ENABLE_LUT_NORMALIZATION_TYPE
#define ENABLE_LUT_NORMALIZATION_TYPE 1
#endif

#ifndef ENABLE_HUE_RESTORATION
#define ENABLE_HUE_RESTORATION 1
#endif

#ifndef ENABLE_POST_PROCESS
#define ENABLE_POST_PROCESS 1
#endif

#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

#if TONEMAP_IN_WIDER_GAMUT && TONEMAP_TYPE == 1
#define COLOR_SPACE CS_BT2020
#else
#define COLOR_SPACE CS_BT709
#endif

float3 DecodeInput(float3 input)
{
#if COLOR_SPACE == CS_BT2020
  input = BT709_To_BT2020(input);
#endif
  return input;
}
float4 DecodeInput(float4 input)
{
  return float4(DecodeInput(input.rgb), input.a);
}
float3 EncodeOutput(float3 output)
{
#if COLOR_SPACE == CS_BT2020
  output = BT2020_To_BT709(output);
#endif
  return output;
}

void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
  float3 sceneColor = DecodeInput(SceneColorTexture.Sample(SceneColorTexture_s, v0.zw).xyz);
  float3 bloomedSceneColor = DecodeInput(BloomOnlyImage.Sample(BloomOnlyImage_s, v1.xy).xyz);
  float3 blurredSceneColor = DecodeInput(DOFAndMotionBlurImage.Sample(DOFAndMotionBlurImage_s, v1.xy).xyz);
  float blurStrength = DOFAndMotionBlurInfoImage.Sample(DOFAndMotionBlurInfoImage_s, v1.xy).z;
  float4 lightShaftsColor = DecodeInput(LightShaftTexture.Sample(LightShaftTexture_s, v0.zw).xyzw);

  float3 mixedSceneColor = lerp(sceneColor, blurredSceneColor, blurStrength) + bloomedSceneColor;
  float logSceneLuminance = min(1.0, exp2(GetLuminance(mixedSceneColor, COLOR_SPACE) * -3.0)); // LUMA: fixed wrong ~BT.601 luminance (0.3,0.59,0.11)
  mixedSceneColor = (mixedSceneColor * lightShaftsColor.w) + (0 * 4.0 * logSceneLuminance);
#if !ENABLE_POST_PROCESS
  mixedSceneColor = sceneColor;
#endif
  mixedSceneColor *= ImageAdjustment.x; // Exposure (e.g. a possible/common value is 1.75)

  float3 tonemappedVanillaSDRColor = mixedSceneColor / abs(mixedSceneColor + 0.187); // Custom Reinhard, this is an approximation of basic Reinhard+gamma 2.2 at once (as such, unless we consider the output as gamma space (which it is), this massively elevates midtones)
  float3 tonemappedSDRColor = tonemappedVanillaSDRColor;
  bool tonemappedSDRColorLinear = false; // It's in gamma 2.2 space by default (theoretically, it should be interpreted as such)
#if FIX_SDR_TONEMAPPER_TYPE >= 2 // Fixes crushed blacks and raised highlights
  tonemappedSDRColor = mixedSceneColor / abs(mixedSceneColor + 1.0); //TODO: why abs()?
  tonemappedSDRColorLinear = true; //TODO: restore some shadow crush
#endif
  float3 tonemappedColor = tonemappedSDRColor;

#if TONEMAP_TYPE >= 1

#if ENABLE_FAKE_HDR // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that
  float normalizationPoint = 1.0; // Found empyrically
  float mixedSceneColorLuminance = GetLuminance(mixedSceneColor, COLOR_SPACE) / normalizationPoint;
  float fakeHDRIntensity = 0.5;
  mixedSceneColorLuminance = mixedSceneColorLuminance > 1.0 ? pow(mixedSceneColorLuminance, 1.0 + fakeHDRIntensity) : mixedSceneColorLuminance;
  mixedSceneColor = RestoreLuminance(mixedSceneColor, mixedSceneColorLuminance * normalizationPoint, COLOR_SPACE);
#endif

  // Match HDR with vanilla tonemaper SDR mid gray
  float outMidGray = linear_to_gamma1(MidGray); // Gamma space (~0.5)
  float inMidGray = -(0.187 * outMidGray) / (outMidGray - 1.0);
  float3 tonemappedHDRColor = mixedSceneColor * (inMidGray / MidGray);

#if TONEMAP_TYPE < 2
  bool tonemappedColorLinear = false;
#if 1 // Blend near black with SDR in linear space
  tonemappedSDRColor = tonemappedSDRColorLinear ? tonemappedSDRColor : gamma_to_linear(tonemappedSDRColor);
#if 0 // This reduces saturation a lot
  tonemappedColor = RestoreLuminance(tonemappedHDRColor, lerp(tonemappedSDRColor, tonemappedHDRColor, saturate(GetLuminance(tonemappedSDRColor, COLOR_SPACE) / MidGray)), false, COLOR_SPACE);
#else
  tonemappedColor = lerp(tonemappedSDRColor, tonemappedHDRColor, saturate(pow(tonemappedSDRColor / MidGray, 0.75)));
  //tonemappedColor = lerp(tonemappedSDRColor, tonemappedHDRColor, saturate(pow(GetLuminance(tonemappedSDRColor, COLOR_SPACE) / MidGray, 0.75))); // This also desaturates too much (or does it???)
#endif
  tonemappedColorLinear = true;
#else
  tonemappedHDRColor = linear_to_gamma(tonemappedHDRColor);
  tonemappedSDRColor = tonemappedSDRColorLinear ? linear_to_gamma(tonemappedSDRColor) : tonemappedSDRColor;
  tonemappedColor = lerp(tonemappedSDRColor, tonemappedHDRColor, saturate(tonemappedSDRColor * 2.0));
#endif

  tonemappedColor *= tonemappedColorLinear ? gamma_to_linear1(1.035) : 1.035; // Random multiplier the vanilla LUT had... we keep it for consistency

  if (!tonemappedColorLinear) tonemappedColor = gamma_to_linear(tonemappedColor);
  tonemappedColor = EncodeOutput(tonemappedColor);
  if (!tonemappedColorLinear) tonemappedColor = linear_to_gamma(tonemappedColor);

  // HDR LUT Extrapolation
  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = tonemappedColor;

  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
  extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
  extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
  extrapolationSettings.lutSize = LUT_SIZE;
  extrapolationSettings.inputLinear = tonemappedColorLinear;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = false;
  extrapolationSettings.outputLinear = true;
  extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2; //TODOFT6: why is this changing colors even with SDR input??? Probably cuz lut sampling was broken. Try again. There's also a tiny diff between our vanilla SDR and the actual vanilla SDR. Maybe it's due to the fixed FXAA luminance
  extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
  extrapolationSettings.samplingQuality = (HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS || ENABLE_LUT_TETRAHEDRAL_INTERPOLATION) ? (ENABLE_LUT_TETRAHEDRAL_INTERPOLATION ? 2 : 1) : 0;
#if LUT_EXTRAPOLATION_QUALITY >= 2
  extrapolationSettings.backwardsAmount = 2.0 / 3.0;
#endif
  // Empirically found value for Prey LUTs. Anything less will be too compressed, anything more won't have a noticieable effect.
  // This helps keep the extrapolated LUT colors at bay, avoiding them being overly saturated or overly desaturated.
  // At this point, Prey can have colors with brightness beyond 35000 nits, so obviously they need compressing.
  //extrapolationSettings.inputTonemapToPeakWhiteNits = 1000.0; // Relative to "extrapolationSettings.whiteLevelNits"
  // Empirically found value for Prey LUTs. This helps to desaturate extrapolated colors more towards their Vanilla (HDR tonemapper but clipped) counterpart, often resulting in a more pleasing and consistent look.
  // This can sometimes look worse, but this value is balanced to avoid hue shifts.
  //extrapolationSettings.clampedLUTRestorationAmount = 1.0 / 4.0;
  // Empirically found value for Prey LUTs. This helps to avoid staying too much from the SDR tonemapper Vanilla colors, which gave certain colors (and hues) to highlights.
  // We don't want to go too high, as SDR highlights hues were very distorted by the SDR tonemapper, and they often don't even match the diffuse color around the scene emitted by them (because it wasn't as bright and thus wouldn't have distorted),
  // so they can feel out of place.
  static const float vanillaLUTRestorationAmount = 1.0 / 3.0;
  //extrapolationSettings.vanillaLUTRestorationAmount = vanillaLUTRestorationAmount;

#if ENABLE_COLOR_GRADING
  outColor.rgb = SampleLUTWithExtrapolation(ColorGradingLUT, ColorGradingLUT_s, extrapolationData, extrapolationSettings);
#if ENABLE_HUE_RESTORATION
  tonemappedVanillaSDRColor = gamma_to_linear(tonemappedVanillaSDRColor);
  tonemappedVanillaSDRColor = EncodeOutput(tonemappedVanillaSDRColor);
  tonemappedVanillaSDRColor = linear_to_gamma(tonemappedVanillaSDRColor);
  tonemappedVanillaSDRColor *= lerp(1.035, 1.0, saturate(tonemappedVanillaSDRColor));
  tonemappedVanillaSDRColor = gamma_to_linear(SampleLUT(ColorGradingLUT, ColorGradingLUT_s, tonemappedVanillaSDRColor, LUT_SIZE));
  outColor.rgb = RestoreHue(outColor.rgb, tonemappedVanillaSDRColor, 0.75); // Values between 0.5 and 0.8 are good for restoration
#endif // ENABLE_HUE_RESTORATION
#if ENABLE_LUT_NORMALIZATION_TYPE >= 1
#if 1 // This generates a huge amount of colors outside of the CIE graph, but preserves shadow saturation
	float3 sourceOklab = linear_srgb_to_oklab(tonemappedColor);
	float3 targetOklab = linear_srgb_to_oklab(outColor.rgb);
	targetOklab.x = lerp(sourceOklab.x, targetOklab.x, 0.333);
#if 1
  outColor.rgb = BT2020_To_BT709(SimpleGamutClip(oklab_to_linear_bt2020(targetOklab), true));
  //outColor.rgb = BT2020_To_BT709(max(oklab_to_linear_bt2020(targetOklab), 0.0));
#else
  outColor.rgb = oklab_to_linear_srgb(targetOklab);
  outColor.rgb = SimpleGamutClip(outColor.rgb, false);
#endif
#else // This reduces the percevied saturation in shadow
  outColor.rgb = RestoreLuminance(outColor.rgb, lerp(GetLuminance(tonemappedColor), GetLuminance(outColor.rgb), 0.25));
#endif
#endif // ENABLE_LUT_NORMALIZATION_TYPE >= 1
#else // !ENABLE_COLOR_GRADING
  outColor.rgb = tonemappedColorLinear ? tonemappedColor : gamma_to_linear(tonemappedColor);
#endif // ENABLE_COLOR_GRADING

#if 0 // WIP desaturation
  float finalLuminance = GetLuminance(outColor.rgb) / MidGray;
  outColor.rgb = Saturation(outColor.rgb, pow(saturate(1.0 / finalLuminance), LumaSettings.DevSetting01 * 3));
#endif

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
	DICESettings settings = DefaultDICESettings();
#if 1
  settings.Type = DICE_TYPE_BY_LUMINANCE_RGB;
  settings.ShoulderStart = (settings.Type > DICE_TYPE_BY_LUMINANCE_RGB) ? (1.0 / 3.0) : 0.0; // Setting it higher than 1/3 might cause highlights clipping as detail is too compressed. Setting it lower than 1/4 would probably look dynamic range. 1/3 seems like the best compromize.
  settings.DesaturationAmount = 1.0 / 3.0; //TODO?
  settings.DarkeningAmount = 1.0 / 3.0;
#endif
  outColor.rgb = DICETonemap(outColor.rgb * paperWhite, peakWhite, settings) / paperWhite; //TODO: try by luminance!
#else // TONEMAP_TYPE == 1
  outColor.rgb = tonemappedHDRColor * gamma_to_linear1(1.035);
#endif // TONEMAP_TYPE < 2

  outColor.a = GetLuminance(outColor.rgb); // Used later by FXAA
    
#if POST_PROCESS_SPACE_TYPE == 0
  outColor.rgb = linear_to_gamma(outColor.rgb);
  outColor.a = linear_to_gamma1(outColor.a);
#endif

#else // TONEMAP_TYPE < 1

  if (tonemappedSDRColorLinear)
  {
    tonemappedColor = linear_to_gamma(tonemappedColor);
  }

#if FIX_SDR_TONEMAPPER_TYPE >= 1 // Avoid clipping highlights
  tonemappedColor *= lerp(1.035, 1.0, saturate(tonemappedColor));
#else // Note that this didn't really make sense as was clipping more colors than needed
  tonemappedColor *= 1.035;
#endif

#if ENABLE_COLOR_GRADING
#if 1
  const bool tetrahedralInterpolation = false; // It doesn't seem to help at all
  outColor.rgb = SampleLUT(ColorGradingLUT, ColorGradingLUT_s, tonemappedColor, LUT_SIZE, tetrahedralInterpolation);
  
#if ENABLE_LUT_NORMALIZATION_TYPE == 1
  outColor.rgb = RestoreLuminance(outColor.rgb, tonemappedColor);
#elif ENABLE_LUT_NORMALIZATION_TYPE >= 2 // This doesn't seem to do anything on most LUTs //TODO: move to LUT file
  float3 vOriginalGamma = outColor.rgb;
  float3 vBlackGamma = SampleLUT(ColorGradingLUT, ColorGradingLUT_s, 0.0, LUT_SIZE, false);
  float3 vMidGrayGamma = SampleLUT(ColorGradingLUT, ColorGradingLUT_s, 0.5, LUT_SIZE, false);
  float3 vWhiteGamma = SampleLUT(ColorGradingLUT, ColorGradingLUT_s, 1.0, LUT_SIZE, false);
  float3 vNeutralGamma = tonemappedColor;
  outColor.rgb = NormalizeLUT(vOriginalGamma, vBlackGamma, vMidGrayGamma, vWhiteGamma, vNeutralGamma);
#endif // ENABLE_LUT_NORMALIZATION_TYPE

#if 0 // Awful in this game
  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  outColor.rgb = linear_to_gamma(PumboAutoHDR(gamma_to_linear(outColor.rgb), peakWhite, paperWhite, 6.667));
#endif

  outColor.a = linear_to_gamma1(GetLuminance(gamma_to_linear(outColor.rgb)));

#else // The original LUT sampling was good, the math was perfect
  float4 r0,r1,r2;
  float3 lutCoords = saturate(tonemappedColor);
  r1.xyw = float3(LUT_MAX, 1.0 - (1.0 / LUT_SIZE), (1.0 / LUT_SIZE) * (LUT_MAX / LUT_SIZE)) * lutCoords.bgr;
  r0.x = floor(r1.x);
  r1.x = r0.x * (1.0 / LUT_SIZE) + r1.w;
  r1.xyzw = float4(0.5 / (LUT_SIZE * LUT_SIZE), 0.5 / LUT_SIZE, (0.5 / (LUT_SIZE * LUT_SIZE)) + (1.0 / LUT_SIZE), 0.5 / LUT_SIZE) + r1.xyxy;
  r0.x = (1.0 / LUT_SIZE) * r0.x;
  r0.x = lutCoords.b * (1.0 - (1.0 / LUT_SIZE)) + -r0.x;
  r0.x = LUT_SIZE * r0.x;
  // Note that the (wrongly calculated) luminance was pre-baked in the LUT's alpha channel
  r2.xyzw = ColorGradingLUT.Sample(ColorGradingLUT_s, r1.zw).xyzw;
  r1.xyzw = ColorGradingLUT.Sample(ColorGradingLUT_s, r1.xy).xyzw;
  outColor.rgba = lerp(r1.xyzw, r2.xyzw, r0.x);
#endif
#else
  outColor.rgb = tonemappedColor;
  outColor.a = linear_to_gamma1(GetLuminance(gamma_to_linear(outColor.rgb)));
#endif

#endif // TONEMAP_TYPE >= 1

#if DEVELOPMENT && 0 // Test LUT gradients
  bool tetrahedralInterpolation2 = LumaSettings.DevSetting02 >= 0.5;
  float z = LumaSettings.DevSetting01;
  outColor.rgb = SampleLUT(ColorGradingLUT, ColorGradingLUT_s, float3(v1.x, v1.y, z * length(v1.xy) / sqrt(2.0)), LUT_SIZE, tetrahedralInterpolation2);
#endif
}