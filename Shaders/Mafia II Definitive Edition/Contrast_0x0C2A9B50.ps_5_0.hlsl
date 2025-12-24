#include "../Includes/Common.hlsl"

#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 2
#endif
#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

cbuffer _Globals : register(b0)
{
  row_major float4x4 g_SMapTM[4] : packoffset(c101);
  float4 g_DbgsceneColor : packoffset(c117);
  float4 g_FilterTaps[8] : packoffset(c118);
  float4 g_FadingParams : packoffset(c126);
  float4 g_CSMRangesSqr : packoffset(c127);
  float2 g_SMapSize : packoffset(c128);
  float4 g_CameraOrigin : packoffset(c129);
  float4 ContrastMode : packoffset(c0);
}

SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

// Within normal color and contrast param ranges, this doesn't really generate negative values, even if it might crush blacks a bit (not to 0 for the most part)
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;

  // Mutually exclusive
  bool contrastMode1 = ContrastMode.y == 0; // Actual contrast - Common one
  bool contrastMode2 = ContrastMode.y == 1; // Increases saturation
  bool contrastMode3 = ContrastMode.y == 2; // Actual contrast (slightly different)
  
  float contrast = ContrastMode.x + ContrastMode.x; // Not sure why it's scaled, maybe because neutral was 0.5 in their editor, but here it's 1. Neutral at 0.
  
#if DEVELOPMENT && 0
  contrastMode1 = DVS7;
  contrastMode2 = DVS8;
  contrastMode3 = DVS9;
  contrast = DVS6 * 2;
#endif

  r1.xyzw = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyzw;
  o0.w = r1.w;
  float3 sceneColor = r1.xyz;
  uint colorSpace = CS_BT709;
#if IMPROVED_COLOR_GRADING_TYPE >= 2 // Do it all in BT.2020, it should look better!
  sceneColor = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(sceneColor, GCT_MIRROR)), GCT_MIRROR);
  colorSpace = CS_BT2020;
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2

#if 1 // Luma: calc luminance in linear space
  float luminance = linear_to_gamma1(GetLuminance(gamma_to_linear(sceneColor, GCT_POSITIVE), colorSpace)); // Don't use "emulatedSceneColor" for higher precision, we clamp it later (hopefully it doesn't expose any broken math)
#else
  float luminance = GetLuminance(sceneColor, colorSpace);
#endif

  float3 emulatedSceneColor = sceneColor;
#if 1 // Luma: added saturate() to emulate and extend vanilla UNORM behaviour
  emulatedSceneColor = max(emulatedSceneColor, 0.0);
  emulatedSceneColor /= max(max3(emulatedSceneColor), 1.0); // This is optional now with the new code to clamp it below! But it might better preserve hues
#elif 1 // Luma: added saturate() to emulate vanilla UNORM behaviour (this is lower quality as it leaves the HDR range unaffected by contrast, at least in some contrast modes)
  emulatedSceneColor = saturate(emulatedSceneColor);
#endif
  if (!contrastMode2) // Luma: clamp luminance and color to ~0.75 so contrast can keep pushing up beyond 1, otherwise 1 would have mapped to 1 with no shift, and all colors above it would have been unaffected (Luma runs TM after so we can afford to expand the range)
  {
    // Note: we use 0.9375 instead of 0.75 because otherwise contrast would be way too strong in HDR highlights, and given it's per channel, it messes up saturation.
    // If we don't do this, highlights wouldn't get boosted and gradients would break.
    luminance = min(luminance, 0.9375);
    emulatedSceneColor = min(emulatedSceneColor, 0.9375);
  }

  // All modes shared code
  float3 luminanceDistance = sceneColor - luminance; // Result is from -1 to +1 (in SDR)
  float3 normalizedLuminanceDistance = luminanceDistance * 0.5 + 0.5; // Result is from 0 to 1 (in SDR)
  float3 tempColor = contrastMode1 ? luminance : (contrastMode2 ? normalizedLuminanceDistance : sceneColor);
  float emulatedluminance = luminance;
#if 1 // Luma: attempt at preventing colors from breaking (emulating vanilla ranges) (makes the branches below redundant) (similar to directly using "emulatedSceneColor")
  tempColor = saturate(tempColor);
  emulatedluminance = saturate(emulatedluminance);
#endif
  float3 tempColor2 = tempColor * (1.5 - tempColor) + 0.5; // Maps 0 to 0.5, 0.5 to 1.0 and 1.0 to 1.0. Values between 0.5 and 1.0 overshoot beyond 1 (a peak of 1.0625 for 0.75 input). This formula doesn't seem to be safe for input values beyond 0-1, it should clip at 0.5.

  // Mode 1
  r3.xyz = tempColor * tempColor2 - emulatedluminance; // Luminance based (contrast around luminance) 

  // Mode 2
  r5.xyz = (((tempColor2 * tempColor) * 2.0 + emulatedluminance) - 1.0) - emulatedSceneColor; // Contrast on RGB from the greyscale? This is weird and likely looks deep fried. Update: it's saturation

  // Mode 3
  r4.xyz = tempColor * tempColor2 - emulatedSceneColor;

  o0.xyz = sceneColor;
#if ENABLE_COLOR_GRADING
  o0.xyz += (contrastMode1 ? r3.xyz : (contrastMode2 ? r5.xyz : (contrastMode3 ? r4.xyz : 0.0))) * contrast;
#endif

#if IMPROVED_COLOR_GRADING_TYPE >= 2

  // Do saturation in linear space (roughly matched in intensity) (the other types are okish to be run in gamma space)
  if (contrastMode2)
  {
    sceneColor = gamma_to_linear(sceneColor, GCT_MIRROR);
    o0.xyz = Saturation(sceneColor, (contrast * 0.25) + 1.0, colorSpace);
  }
  else
  {
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
  }
  
  o0.xyz = linear_to_gamma(BT2020_To_BT709(o0.rgb), GCT_MIRROR);

#endif // IMPROVED_COLOR_GRADING_TYPE >= 2
}