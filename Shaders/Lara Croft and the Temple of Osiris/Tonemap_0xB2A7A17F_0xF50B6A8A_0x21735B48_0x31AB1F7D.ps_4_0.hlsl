#define LUT_3D 1

#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Reinhard.hlsl"

#ifndef LUT_SAMPLING_ERROR_EMULATION_MODE
#define LUT_SAMPLING_ERROR_EMULATION_MODE 2
#endif

cbuffer cbColCor : register(b5)
{
  float4 g_VigColIn : packoffset(c0);
  float4 g_VigColOut : packoffset(c1);
  float2 g_vScale0 : packoffset(c2);
  float2 g_vOffs0 : packoffset(c2.z);
  float2 g_vScale1 : packoffset(c3);
  float2 g_vOffs1 : packoffset(c3.z);
  float g_fDualBlend : packoffset(c4);
  float3 g_vPad0 : packoffset(c4.y);
  float4 g_Params : packoffset(c5);
  float4 g_VigParams : packoffset(c6);
  float2 g_vGrainScale : packoffset(c7);
  float2 g_vGrainOffs : packoffset(c7.z);
  float3 g_vLutBlends : packoffset(c8);
  uint g_uLutCount : packoffset(c8.w);
}

SamplerState SamplerGenericPointWrap_s : register(s9);
SamplerState SamplerGenericBilinearClamp_s : register(s13);
Texture2D<float4> colorBuffer : register(t0);
Texture2D<float4> lookupTexture : register(t1);
Texture2D<float4> noiseTexture : register(t2);
Texture3D<float4> texLut0 : register(t3);
Texture3D<float4> texLut1 : register(t4);
Texture3D<float4> texLut2 : register(t5);

float3 ApplyLUT(float3 color, Texture3D<float4> _texture, SamplerState _sampler)
{
  float3 postLutColor;

#if 1 // Luma: lut extrapolation

#if LUT_SAMPLING_ERROR_EMULATION_MODE > 0 // Fix bad math in lut sampling that crushed blacks, we emulate it now
    float3 previousColor = color;
    float adjustmentScale = 0.4; // 0.25 looks good in indoor scenes, but below 0.5 it looks nasty outdoors if we do it per channel, so we do it in linear
    float adjustmentRange = 1.0 / 3.0;
    float adjustmentPow = 1.0;
#if LUT_SAMPLING_ERROR_EMULATION_MODE != 2 // Per channel (it looks nicer)
    color *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma(previousColor, GCT_POSITIVE) / adjustmentRange, adjustmentPow)));
#else // LUT_SAMPLING_ERROR_EMULATION_MODE == 2 // By luminance
    color *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma1(max(GetLuminance(previousColor), 0.0)) / adjustmentRange, adjustmentPow)));
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 2
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE > 0

  bool lutExtrapolation = true;
#if DEVELOPMENT
  lutExtrapolation = DVS10 <= 0.5;
#endif
  if (lutExtrapolation)
  {
    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = color.rgb;
    extrapolationData.vanillaInputColor = saturate(color.rgb);
  
    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();

    // LUTs were encoded with gamma 2.2 on input, and outputted a linear sRGB color, given they were UNORM_SRGB,
    // so we should theoretically fix the mismatch here too, however that would raise blacks, and with the fact that we fixed
    // the broken sampling math that clipped near black values, we are already raised even when leaving luts output as 2.2, even if we have further adjustments for it above.
    // 
    // The reality is that LUTs would have been authored on gamma 2.2 displays with gamma 2.2 in and out,
    // and then coverted to UNORM_SRGB by the engine, hence their output is distorted compared to what the grading artists saw (blacks would get raised here) (unless they graded directly in engine that is, but that's highly unlikely).
    // This is a weird case, because the image is encoded with 2.2, then LUTs interpret that as sRGB and covert it to linear, the swapchain is UNORM_SRGB (linear), and then the display would have linearized with 2.2 again,
    // hence applying LUTs would have cancelled the gamma mismatch from using a sRGB swapchain. However LUTs aren't always applied so sometimes there's a gamma mismatch, and other times there isn't...
    // The best solution is probably to linearize at the end with 2.2 anyway, that's how the game would have looked in SDR, at all times (whether it was good or not).
    extrapolationSettings.lutSize = 0;
    extrapolationSettings.inputLinear = true;
    extrapolationSettings.lutInputLinear = false;
    extrapolationSettings.lutOutputLinear = true;
    extrapolationSettings.outputLinear = true;
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
    extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;

#if 1 // High quality
    extrapolationSettings.samplingQuality = 1;
    extrapolationSettings.extrapolationQuality = 2;
#endif
  
    postLutColor = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);
  }
  else
  {
    float3 size;
    _texture.GetDimensions(size.x, size.y, size.z); // 32x but we can never be too sure
    const float3 lutMax = size.x - 1.0;
    const float3 lutInvSize = 1.0 / size;
    const float3 lutCoordsOffset = lutInvSize * 0.5;
    float3 lutInColor = linear_to_gamma(color, GCT_SATURATE);
    float3 lutCoords3D = (saturate(lutInColor) * lutMax * lutInvSize) + lutCoordsOffset;
    postLutColor = _texture.SampleLevel(_sampler, lutCoords3D, 0).rgb; // Sampling here will slightly raise blacks as it's 2.2 in and sRGB out (we take care of it above)
    
    float hueRestoration = 0.0;
    bool restorePostProcessInBT2020 = true;
#if DEVELOPMENT
    hueRestoration = LumaSettings.DevSetting04;
    restorePostProcessInBT2020 = LumaSettings.DevSetting05 <= 0.5;
#endif
    postLutColor = RestorePostProcess(color, saturate(color), postLutColor, hueRestoration, restorePostProcessInBT2020);
  }

#else // Vanilla: broken sampling, it doesn't acknowledge the half texel offset, crushing blacks

  float3 lutInColor = linear_to_gamma(color, GCT_SATURATE);
  postLutColor = _texture.SampleLevel(_sampler, lutInColor, 0).rgb;

#endif
  
  return postLutColor;
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;

  float2 size;
  colorBuffer.GetDimensions(size.x, size.y);
  float2 uv = v0.xy / size;
  int3 pixelCoords = int3(v0.xy, 0);

#if _21735B48 || _31AB1F7D
  float2 uv1 = uv * g_vScale0.xy + g_vOffs0.xy;
  float2 uv2 = uv * g_vScale1.xy + g_vOffs1.xy;
  float3 sceneColor1 = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, uv1, 0).rgb;
  float3 sceneColor2 = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, uv2, 0).rgb;
  float3 sceneColor = lerp(sceneColor1, sceneColor2, g_fDualBlend);
#else
  float3 sceneColor = colorBuffer.Load(pixelCoords).rgb;
#endif

  float3 tonemappedColor = sceneColor;
  
#if (_F50B6A8A || _31AB1F7D) && 1 // Contrast etc LUT, this does ugly fades to black and raises blacks too, causing visible changes from one frame to the next but whatever
#if 1 // Luma: LUT extrapolation
  float lutWidth;
  float lutHeight;
  lookupTexture.GetDimensions(lutWidth, lutHeight); // 256x2
  float xScale = (lutWidth - 1.f) / lutWidth;
  float xOffset = 1.f / (2.f * lutWidth);

  uint colorChannel = 3; // Use w/a
  float3 lutColor = Sample1DLUTWithExtrapolation(lookupTexture, SamplerGenericBilinearClamp_s, tonemappedColor, 0, 0, 0, colorChannel, colorChannel, colorChannel, colorChannel).rgb;
#else // Vanilla: broken sampling, it doesn't acknowledge the half texel offset, crushing blacks
  float lutRed = lookupTexture.SampleLevel(SamplerGenericBilinearClamp_s, float2(tonemappedColor.r, 0.25), 0).w; // Sample W (it's possible RGBA are all the same)
  float lutGreen = lookupTexture.SampleLevel(SamplerGenericBilinearClamp_s, float2(tonemappedColor.g, 0.25), 0).w;
  float lutBlue = lookupTexture.SampleLevel(SamplerGenericBilinearClamp_s, float2(tonemappedColor.b, 0.25), 0).w;
  float3 lutColor = float3(lutRed, lutGreen, lutBlue);
#endif
  float lutPeakIn = max3(lutColor);
#if 1 // Luma
  float4 lutPeakOut;
  // TODO: LUT extrapolation seems to make little sense here? Should we just clamp to avoid issues? Does this even influence the colors or does it just do a fade?
  lutPeakOut.rgba = Sample1DLUTWithExtrapolation(lookupTexture, SamplerGenericBilinearClamp_s, lutPeakIn, 1, 1, 1, 0, 1, 2, 3, true).rgba;
  //float4 lutPeakOut = lookupTexture.SampleLevel(SamplerGenericBilinearClamp_s, float2((lutPeakIn * xScale) + xOffset, 0.75), 0).rgba; // Let the input clip
#else
  float4 lutPeakOut = lookupTexture.SampleLevel(SamplerGenericBilinearClamp_s, float2(lutPeakIn, 0.75), 0).rgba;
#endif
  float alpha = -lutPeakOut.w * 2.0 + 1.0; // From -1 to 1 (weird)
  float3 lutModulatedColor = (lerp(lutColor, lutPeakIn, alpha)); // Luma: we removed the saturate but I it might be better with it?
  float lutPeakOutInversePeak = 1.0 - max3(lutPeakOut.rgb);
#if 1 // Luma: made blending safe for HDR values
  tonemappedColor = (lutModulatedColor * saturate(lutPeakOutInversePeak)) + (lutPeakOut.rgb * saturate(lutPeakIn));
#else
  tonemappedColor = (lutModulatedColor * lutPeakOutInversePeak) + (lutPeakOut.rgb * lutPeakIn);
#endif

  float tonemappedColorPeak = lutPeakIn;

#else

  float tonemappedColorPeak = max3(tonemappedColor);

#endif

#if 1 // Film grain or noise/dither
  r1.zw = uv * g_vGrainScale.xy + g_vGrainOffs.xy;
  float3 grainColor = noiseTexture.SampleLevel(SamplerGenericPointWrap_s, r1.zw, 0).rgb;
  grainColor = ((g_Params.w * sqr(1 - tonemappedColorPeak)) * grainColor) + 1.0; // TODO: grain will be at minimum when the scene peak brightness is 1, but will increase again for values beyond it, so that makes no sense, anyway film grain should be on highlights more than shadows
  tonemappedColor *= grainColor;
#endif

#if 1 // Color grading (also converts to gamma space, if it's not done here, it's supposedly done at the end in the swapchain copy shader?)
  if (g_uLutCount != 0) {
    float3 lutInColor = tonemappedColor;
    tonemappedColor = ApplyLUT(lutInColor, texLut0, SamplerGenericBilinearClamp_s); // First lut is always fully applied, only 1 and 2 have intensiy
    // Note: these blends aren't really good, because they blend each LUT in sequence instead of doing it in parallel, so the last one can override the previous ones fully, and starts from their results.
    // However, the game was probably designed for that so fixing the logic might be detrimental.
    if (g_uLutCount >= 1) {
      float3 secondaryLutColor = ApplyLUT(lutInColor, texLut1, SamplerGenericBilinearClamp_s);
      tonemappedColor = lerp(tonemappedColor, secondaryLutColor, g_vLutBlends.x);
      if (g_uLutCount >= 2) {
        secondaryLutColor = ApplyLUT(lutInColor, texLut2, SamplerGenericBilinearClamp_s);
        tonemappedColor = lerp(tonemappedColor, secondaryLutColor, g_vLutBlends.y);
      }
    }
  }
#endif

#if 1 // Luma: Tonemapping (before vignette!) (duplicate in FXAA)
  if (LumaSettings.DisplayMode == 1)
  {
    float normalizationPoint = 0.02; // Found empyrically
    float fakeHDRIntensity = 0.2; // TODO: expose?
    float fakeHDRSaturation = 0.2;
    tonemappedColor = FakeHDR(tonemappedColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
  }

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  bool tonemapPerChannel = LumaSettings.DisplayMode != 1; // Vanilla clipped (hue shifted) look is better preserved with this
  if (LumaSettings.DisplayMode == 1)
  {
    DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    tonemappedColor = DICETonemap(tonemappedColor * paperWhite, peakWhite, settings) / paperWhite;
  }
  else
  {
     float shoulderStart = 0.333; // Set it higher than "MidGray", otherwise it compresses too much.
    if (tonemapPerChannel)
    {
      tonemappedColor = Reinhard::ReinhardRange(tonemappedColor, shoulderStart, -1.0, peakWhite / paperWhite, false);
    }
    else
    {
      tonemappedColor = RestoreLuminance(tonemappedColor, Reinhard::ReinhardRange(GetLuminance(tonemappedColor), shoulderStart, -1.0, peakWhite / paperWhite, false).x, true);
      tonemappedColor = CorrectOutOfRangeColor(tonemappedColor, true, true, 0.5, peakWhite / paperWhite);
    }
  }
#endif

#if 1 // Vignette
  r1.xy = -g_VigParams.zw + uv;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r0.w = -g_VigParams.x + r0.w;
  r0.w = saturate(g_VigParams.y * r0.w);
  r1.xyzw = g_VigColOut.xyzw + -g_VigColIn.xyzw;
  float4 vignetteColorAndAlpha = r0.w * r1.xyzw + g_VigColIn.xyzw;
  tonemappedColor = lerp(tonemappedColor, vignetteColorAndAlpha.rgb, vignetteColorAndAlpha.a);
#endif
  
  o0.rgb = tonemappedColor;
  o0.w = 1; // Always the case

#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
  o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif
}