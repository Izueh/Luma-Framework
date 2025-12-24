#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

// TODO: turning this off causes issues (e.g. start the first DLC and check the reflection in the main character glasses), this workaround seems to work fine (though no, it moves around the wide gamut, maybe it's fine!)
#ifndef EMULATE_VANILLA
#define EMULATE_VANILLA 1
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

// TODO: test inverted colors on death... They might be from this shader, or some one before/after
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  o0.w = 1;

  float4 r0,r1,r2;
  r0.xyz = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyz;
#if EMULATE_VANILLA
  float3 signs = sign(r0.xyz);
  r0.xyz = abs(r0.xyz);
  float peak = max(max3(r0.xyz), 1.0);
  r0.xyz /= peak;
#endif
  r0.xyz *= exp2(gammaExpSatBleach.y);
#if EMULATE_VANILLA
  r0.xyz = pow(abs(r0.xyz), gammaExpSatBleach.x);
#else
  r0.xyz = pow(abs(r0.xyz), gammaExpSatBleach.x) * sign(r0.xyz); // Luma: mirrored negative values
#endif
  float luminance = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_POSITIVE))); // Luma: calc luminance in linear space
#if EMULATE_VANILLA
  r1.xyzw = 1.0 - float4(r0.xyz, luminance);
#else
  float vanillaMaxInput = pow(max(exp2(gammaExpSatBleach.y), 0.0), gammaExpSatBleach.x);
  r1.xyzw = 1.0 - clamp(float4(r0.xyz, luminance), 0.0, vanillaMaxInput); // Luma: clamp to the max range this would have had in vanilla SDR
#endif
  r1.xyz = 1.0 - (r1.w + r1.w) * r1.xyz;
  r2.xyz = luminance * r0.xyz;
  r0.w = luminance - 0.45;
  r0.w = saturate(10 * r0.w);
  r1.xyz = -r2.xyz * 2.0 + r1.xyz;
  r2.xyz = r2.xyz + r2.xyz;
  r1.xyz = r0.w * r1.xyz + r2.xyz;
  r2.xyz = gammaExpSatBleach.w * r0.xyz;
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = -gammaExpSatBleach.w * r0.xyz + 1.0;
  r0.xyz += r2.xyz * r1.xyz;
  float3 colorAverageDiff = r0.xyz - average(r0.xyz);
  r0.xyz += colorAverageDiff * gammaExpSatBleach.z;
  r1.xyz = colorAverageDiff * gammaExpSatBleach.z + 1.0;
  o0.xyz = r0.xyz / r1.xyz;
#if EMULATE_VANILLA
  o0.xyz *= peak * signs;
#endif

  // Luma tonemapping (this is the final shader before UI):

  bool forceVanillaSDR = ShouldForceSDR(v1.xy);
  
  o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
  
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
  FixColorGradingLUTNegativeLuminance(o0.xyz.rgb);
  
#if UI_DRAW_TYPE == 2
  o0.xyz *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2
  
  o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
}