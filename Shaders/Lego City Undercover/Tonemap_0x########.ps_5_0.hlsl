#define LUT_3D 1

#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Reinhard.hlsl"

// 0 "None"
// 1 By Channel
// 2 By Luminance
#ifndef VANILLA_LOOK_TYPE
#define VANILLA_LOOK_TYPE 1
#endif

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef LUT_SAMPLING_ERROR_EMULATION_MODE
#define LUT_SAMPLING_ERROR_EMULATION_MODE VANILLA_LOOK_TYPE
#endif

#if _0E0294C0 || _64A0A446 || _6E2E91CE || _721BE532 || _BC560C2C || _C8EB534B || _1A64CEA7
#define DOF 1
#endif

// Edge case. This permutation is (sometimes?) run again after the main toneampping to write in the DoF alpha, given that FXAA is run separately on DoF and Scene (???)
#if _1A64CEA7
#define DOF_ALPHA_ONLY 1
#endif

#if _48F00D4F || _64A0A446 || _BA76349B || _BC560C2C || _C8EB534B || _F72115F0
#define BLOOM 1
#endif

#if _0E0294C0 || _48F00D4F || _635EB656 || _64A0A446 || _6E2E91CE || _721BE532 || _94DFB540 || _BA76349B || _BC560C2C || _C8EB534B || _CCCCD97F || _F72115F0
#define TONEMAP 1
#endif

#if _0E0294C0 || _48F00D4F || _635EB656 || _64A0A446 || _6E2E91CE || _94DFB540 || _BA76349B || _BC560C2C
#define LUT 1
#endif

#ifndef DOF
#define DOF 0
#endif
#ifndef DOF_ALPHA_ONLY
#define DOF_ALPHA_ONLY 0
#endif
#ifndef BLOOM
#define BLOOM 0
#endif
#ifndef TONEMAP
#define TONEMAP 0
#endif
#ifndef LUT
#define LUT 0
#endif

cbuffer g_MainFilterPS_CB : register(b0)
{
  struct
  {
    float4 mainFilterToneMapping;
    float4 mainFilterDof;
    float4 edgeFilterParams;
    float4 pixel_size;
  } g_MainFilterPS : packoffset(c0);
}

SamplerState fullColor_tex_ss_s : register(s0);
SamplerState blur_tex_ss_s : register(s3);
SamplerState mipColor1_tex_ss_s : register(s4);
SamplerState cubeTex_ss_s : register(s7);
Texture2D<float4> fullColor_tex : register(t0); // Scene
Texture2D<float4> blur_tex : register(t3); // Bloom
Texture2D<float4> mipColor1_tex : register(t4); // DoF
Texture3D<float4> cubeTex : register(t7); // 3D LUT (64x32x16)

float3 ApplyLUT(float3 color, float3 vanillaColor, Texture3D<float4> _texture, SamplerState _sampler, bool forceVanillaSDR = false)
{
  float3 postLutColor;

  if (!forceVanillaSDR)
  {
#if LUT_SAMPLING_ERROR_EMULATION_MODE > 0 // Fix bad math in lut sampling that crushed blacks, we emulate it now
    float3 previousColor = color;
    float adjustmentScale = 0.333; // TODO: refine this (theoretically it should be per channel too, given the game's LUT has 3 different sizes)
    float adjustmentRange = 1.0 / 3.0;
#if LUT_SAMPLING_ERROR_EMULATION_MODE != 2 // Per channel (it looks nicer)
    color *= lerp(adjustmentScale, 1.0, saturate(linear_to_gamma(previousColor, GCT_POSITIVE) / adjustmentRange));
#else // LUT_SAMPLING_ERROR_EMULATION_MODE == 2 // By luminance
    color *= lerp(adjustmentScale, 1.0, saturate(linear_to_gamma1(max(GetLuminance(previousColor), 0.0)) / adjustmentRange));
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 2
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE > 0

    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = color.rgb;
    extrapolationData.vanillaInputColor = vanillaColor;
    
    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();

    bool lutExtrapolation = !forceVanillaSDR;
#if DEVELOPMENT
    lutExtrapolation = DVS10 <= 0.5;
#endif
    extrapolationSettings.enableExtrapolation = lutExtrapolation;

    extrapolationSettings.lutSize = 0;
    extrapolationSettings.inputLinear = false;
    extrapolationSettings.lutInputLinear = false;
    extrapolationSettings.lutOutputLinear = false;
    extrapolationSettings.outputLinear = false;
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
    extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
    
    extrapolationSettings.vanillaLUTRestorationAmount = 0.666; // >= 0.5 looks good, LUT extrapolation hue shifts too much otherwise
    extrapolationSettings.vanillaLUTRestorationType = 1; // Looks better here

#if 1 // High quality
    extrapolationSettings.samplingQuality = 1;
    extrapolationSettings.extrapolationQuality = 2;
#endif
    
    postLutColor = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);
  }
  else // Vanilla: broken sampling, it doesn't acknowledge the half texel offset, crushing blacks
  {
    postLutColor = _texture.SampleLevel(_sampler, color, 0).rgb;
  }
  
  return postLutColor;
}

float3 FilmicTonemap(float3 color)
{
  float4 r1,r2,r3;
  r1.xyz = color * 0.5 + 0.12;
  r2.xyz = float3(5,5,5) * color;
  r1.xyz = r2.xyz * r1.xyz + 0.004;
  r3.xyz = color * 0.5 + 0.4;
  r2.xyz = r2.xyz * r3.xyz + 0.06;
  r1.xyz = (r1.xyz / r2.xyz) - (1.0 / 15.0);
  r1.xyz = float3(1.33959937,1.33959937,1.33959937) * r1.xyz;
  return r1.xyz;
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  float3 sceneColor = fullColor_tex.Sample(fullColor_tex_ss_s, v1.xy).xyz;
#if 0 // Luma: disabled
  sceneColor = max(0.0, sceneColor);
#endif

  float3 tonemappedColor = sceneColor;

  bool forceVanillaSDR = ShouldForceSDR(v1.xy); // Note: this might not work in split screen!
#if 1 // Our SDR tonemapper doesn't really look any better and it'd need to do to much work so for now SDR is purely vanilla
  if (LumaSettings.DisplayMode != 1)
    forceVanillaSDR = true;
#endif

#if DOF
  float4 dofColor = mipColor1_tex.Sample(mipColor1_tex_ss_s, v1.xy).xyzw;
#if 0 // Luma: disabled
  dofColor = max(0.0, dofColor);
#endif
  float dofIntensity = dofColor.a * g_MainFilterPS.mainFilterDof.x + g_MainFilterPS.mainFilterDof.y;
  float dofBlend = saturate(dofIntensity * 2 - 1);
  dofIntensity = saturate(dofIntensity * 2.0);
#if !DOF_ALPHA_ONLY
  tonemappedColor = lerp(tonemappedColor, dofColor.rgb, dofBlend);
#endif
#endif // DOF

#if BLOOM
  float3 bloomColor = blur_tex.Sample(blur_tex_ss_s, v1.xy).xyz;
#if 0 // Luma: disabled (we don't need it as bloom is not subtractive)
  bloomColor = max(float3(0,0,0), bloomColor);
#endif
  float lumaBloomIntensity = 1.0; // TODO: expose if necessary, though bloom is already toggleable
  tonemappedColor += sqr_mirrored(bloomColor) * lumaBloomIntensity; // Undo bloom encoding
#endif // BLOOM

  float3 vanillaTonemappedColor = tonemappedColor;
#if TONEMAP
  float tonemappedColorLuminance;
  bool doTonemap = g_MainFilterPS.mainFilterToneMapping.w > 0;
  // Not sure if the game ever skipped tonemapping, maybe if it used this shader to do grading in linear etc
  if (doTonemap)
  {
    // filmic (?) tonemapper, it also kinda applies gamma directly. Looks quite washed out.
    float3 SDRGammaSpaceColor = FilmicTonemap(tonemappedColor);

    // Luma: Pre-tonemapping
    if (!forceVanillaSDR)
    {
#if ENABLE_HDR_BOOST
      if (LumaSettings.DisplayMode == 1)
      {
        float normalizationPoint = 0.025; // Found empyrically
        float fakeHDRIntensity = 0.2;
        float fakeHDRSaturation = 0.25;
        tonemappedColor = FakeHDR(tonemappedColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
      }
#endif

      // Pre-shift the HDR to match SDR mid grey
      float linearMidGreyIn = 0.271; // gamma_to_linear(FilmicTonemap(0.271)) returns ~0.18
      float linearMidGreyOut = MidGray;
      tonemappedColor *= linearMidGreyOut / linearMidGreyIn;
      
      // Blend in SDR tonemapper below mid grey (this doesn't really look good with custom SDR)
      // Overall, the SDR tonemapper was just bad and didn't add much of value to the look, though it slightly crushes shadow
      float3 SDRColor = gamma_to_linear(SDRGammaSpaceColor, GCT_MIRROR);
#if VANILLA_LOOK_TYPE == 1 // By channel
      tonemappedColor = lerp(SDRColor, tonemappedColor, sqr(saturate(SDRColor / MidGray)));
#elif VANILLA_LOOK_TYPE == 2 // By luminance
      tonemappedColor = lerp(RestoreLuminance(tonemappedColor, GetLuminance(SDRColor)), tonemappedColor, sqr(saturate(GetLuminance(SDRColor) / MidGray)));
#endif

      // Any final post processing, do here (or before the SDR tonemapper too)
      //tonemappedColor = Saturation(tonemappedColor, 3.0);
      
      tonemappedColorLuminance = linear_to_gamma1(GetLuminance(tonemappedColor), GCT_POSITIVE);
      tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
    }
    else
    {
      tonemappedColor = SDRGammaSpaceColor;
      tonemappedColorLuminance = linear_to_gamma1(GetLuminance(gamma_to_linear(SDRGammaSpaceColor, GCT_MIRROR)), GCT_POSITIVE); // Luma: fixed BT.601 luminance // TODO: this was calculated in gamma space. Fix it in other places too!
    }
    
    vanillaTonemappedColor = SDRGammaSpaceColor;
  }
  else
  {
    tonemappedColorLuminance = GetLuminance(tonemappedColor); // Linear to linear
  }

#if 0 // In some permutations, the game did this nonsense, after calculating the luminance again with a slightly different BT.601 formula, it did math that simply accounts so a saturate once simplified
  tonemappedColor = saturate(tonemappedColorLuminance - (tonemappedColorLuminance - tonemappedColor));
#endif
#endif // TONEMAP

#if LUT
  tonemappedColor = ApplyLUT(tonemappedColor, vanillaTonemappedColor, cubeTex, cubeTex_ss_s, forceVanillaSDR); // TODO: expose color grading percentage?
#endif // LUT

#if TONEMAP
  // Luma: Tonemapping (do it after the LUT for consistency)
  if (doTonemap)
  {
    if (!forceVanillaSDR)
    {
      tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);

      const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
      const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
      bool tonemapPerChannel = true; // Game kinda looks better with per channel in most scenes, it was very washed out so desaturation and highlights hue shifts are expected (I should try again though)
      if (LumaSettings.DisplayMode == 1)
      {
        DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
        tonemappedColor = DICETonemap(tonemappedColor * paperWhite, peakWhite, settings) / paperWhite;
      }
      else
      {
        float shoulderStart = MidGray; // Set it higher than "MidGray", otherwise it compresses too much.
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
      
      // Luma: do luminance after LUT, it made no sense it was done before
      tonemappedColorLuminance = linear_to_gamma1(GetLuminance(tonemappedColor), GCT_POSITIVE);
      
      tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
    }
  }
#endif

  // Output
  o0.xyz = tonemappedColor;
#if DOF && TONEMAP
  o0.w = (dofIntensity > g_MainFilterPS.mainFilterDof.z) ? dofIntensity : tonemappedColorLuminance; // Unclear if this is for FXAA or what, it seems like AA is split between DoF distances
#elif DOF
  o0.w = dofIntensity;
#else
  o0.w = tonemappedColorLuminance; // Luminance for FXAA
#endif // DOF && TONEMAP

  o0 = IsNaN_Strict(o0) ? 0.0 : o0; // Luma: add NaNs protection
}