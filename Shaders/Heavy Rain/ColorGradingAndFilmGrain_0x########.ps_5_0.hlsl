#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer ConstentValue : register(b0)
{
  float4x4 register0 : packoffset(c0);
  float4 register4 : packoffset(c4);
  float4 register5 : packoffset(c5);
  float4 register6 : packoffset(c6);
  float4 register7 : packoffset(c7);
  float4 register8 : packoffset(c8);
}

SamplerState sampler0_s : register(s0);
SamplerState sampler1_s : register(s1);
SamplerState sampler2_s : register(s2);
SamplerState Add2Sampler_s : register(s3);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);
Texture2D<float4> texture3 : register(t3);

// Common or sensible defaults
#define FILM_GRAIN 0
#define COLOR_GRADING 0
#define COMPOSE_EFFECTS 0
#define ALPHA_OUT_TYPE 2

#if _A4E4EBA1 || _E6FC219C || _E7CF3D21 || _146F3D1E
#undef FILM_GRAIN
#define FILM_GRAIN 1
#endif

#if _A4E4EBA1 || _D7C7F000 || _E6FC219C || _4F74080B
#undef COLOR_GRADING
#define COLOR_GRADING 1
#endif

#if _D7C7F000 || _E6FC219C || _146F3D1E || _80408373
#undef COMPOSE_EFFECTS
#define COMPOSE_EFFECTS 1
#endif

// Permutation 7DF19F48 has no features enabled!

// Default "ALPHA_OUT_TYPE" was 2, so only changes for the hashes that need it
// 0 and 1 types are for UI sprites (it's not exactly the same shader, but it was similar enough that we merged them)!
#if _B8164665
#undef ALPHA_OUT_TYPE
#define ALPHA_OUT_TYPE 1
#endif
#if _8D4D1C88
#undef ALPHA_OUT_TYPE
#define ALPHA_OUT_TYPE 0
#endif

void main(
  float4 v0 : SV_POSITION0,
#if ALPHA_OUT_TYPE >= 2
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD2,
  float4 v2 : TEXCOORD1,
#else
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
#endif
  out float4 o0 : SV_TARGET0
#if ALPHA_OUT_TYPE < 2
  ,
  out float4 o1 : SV_TARGET1,
  out float o2 : SV_TARGET2
#endif
  )
{
#if ALPHA_OUT_TYPE < 2
  o1.xyzw = float4(0,0,0,0);
  o2.x = v0.z;
#endif

#if FILM_GRAIN // Film grain (it runs at 24fps or something)
  float resolutionScale = 1.0;
#if ENABLE_LUMA
  // For some reason this was already using a linear sampler
  float w, h;
  texture0.GetDimensions(w, h);
  resolutionScale = 1080.0 / h; // Base film grain on 1080p, and scale it after
#endif // ENABLE_LUMA
  float grainMax = texture1.Sample(sampler1_s, v2.zw).x;
  float grainMin = texture1.Sample(sampler1_s, v2.xy * resolutionScale).x;
  float grainRange = grainMax - grainMin;
  float grain = (register4.x * grainRange + grainMin) - register4.y;
#if !ENABLE_FILM_GRAIN
  grain = 0.0;
#endif // !ENABLE_FILM_GRAIN
#endif // FILM_GRAIN
  
#if ALPHA_OUT_TYPE == 2
  float2 uv = v1.xy;
#else
  float2 uv = v2.xy;
#endif
  bool forceVanillaSDR = ShouldForceSDR(uv);
  float4 sceneColor = texture0.Sample(sampler0_s, uv).rgba;
#if ALPHA_OUT_TYPE == 2
  o0.w = sceneColor.a * register5.x + register5.y;
#elif ALPHA_OUT_TYPE == 1
  sceneColor.rgba *= v1.rgba;
  o0.w = sceneColor.a;
#else
  sceneColor.rgb *= v1.rgb;
  o0.w = 1.0;
#endif

#if COMPOSE_EFFECTS && ENABLE_POST_PROCESS_EFFECTS // Composes different effects
  if (forceVanillaSDR)
    sceneColor.rgb += saturate(texture2.Sample(sampler2_s, w1.xy).xyz);
  else
    sceneColor.rgb += texture2.Sample(sampler2_s, w1.xy).xyz * LumaSettings.GameSettings.BloomAndLensFlareIntensity; // Scale in gamma space
  sceneColor.rgb += texture3.Sample(Add2Sampler_s, w1.xy).xyz; // Additive screen space effects (e.g. low health overlay)
#endif // COMPOSE_EFFECTS

  // This fixes NaNs expanding in the pause menu too!
  sceneColor.xyz = IsNaN_Strict(sceneColor.xyz) ? 0.0 : sceneColor.xyz;

#if FILM_GRAIN
#if ENABLE_LUMA // Boost film grain in highlights and remove it from mid tones
  float highlightsScale = 2.5; // > 1 looks good, but makes film grain very strong on highlights
  if (!forceVanillaSDR)
    grain *= linear_to_gamma1(max(GetLuminance(gamma_to_linear(sceneColor.rgb, GCT_MIRROR) * highlightsScale), 0.0)); // Note: try with a pow on "grain" instead?
#endif // ENABLE_LUMA
  const float4 postProcessedColor = float4(sceneColor.rgb + grain * register4.z, 1.0);
#else
  const float4 postProcessedColor = float4(sceneColor.rgb, 1.0);
#endif // FILM_GRAIN

#if 0 // Test: untonemapped passthrough
  o0.rgb = postProcessedColor.rgb; return;
#endif
  
  float3 gradedSceneColor = postProcessedColor.rgb;
#if ENABLE_COLOR_GRADING // Mostly a global desaturation pass (usually)
  float4 filter;
  filter.x = register0._m00;
  filter.y = register0._m01;
  filter.z = register0._m02;
  filter.w = register0._m03;
  gradedSceneColor.r = dot(postProcessedColor, filter.xyzw);
  filter.x = register0._m10;
  filter.y = register0._m11;
  filter.z = register0._m12;
  filter.w = register0._m13;
  gradedSceneColor.g = dot(postProcessedColor, filter.xyzw);
  filter.x = register0._m20;
  filter.y = register0._m21;
  filter.z = register0._m22;
  filter.w = register0._m23;
  gradedSceneColor.b = dot(postProcessedColor, filter.xyzw);
  // TODO: in a couple scenes, highlights on skin turn green/red/purple, for example in the first shrink section, or when Ethan first has an Origami blackout (in front of Shaun's room), it's likely these values would have clipped to white in SDR
#endif // ENABLE_COLOR_GRADING

#if COLOR_GRADING && ENABLE_COLOR_GRADING // Highlights boost and shadow tint
#if 0 // Luma: add saturate on source color to clip to original range, it seems to look best
  const float postProcessedColorAverage = dot(saturate(postProcessedColor.rgb), 1.0 / 3.0);
#elif 0 // Alterantive version that clips after, might look different from Vanilla, but maybe better
  const float postProcessedColorAverage = saturate(dot(postProcessedColor.rgb, 1.0 / 3.0));
#else // Leave it as it was given it wasn't clamped in vanilla
  const float postProcessedColorAverage = dot(postProcessedColor.rgb, 1.0 / 3.0); // rgb average
#endif
  // If "register7" is 1 and "register6" is 0, there's no tint. This doesn't seem to be used that aggressively nor often (?).
  // It's similar to levels.
  float3 shadowTint = saturate(postProcessedColorAverage * register6.xyz + register7.xyz); // Leave this saturate in as it's just a multiplier (it actually acts as an additive offset as it's applied to inverted shadow), without it, in at least one direction it will generate broken colors

  // This will generate colors beyond Rec.709 in the shadow
#if ENABLE_LUMA

  if (forceVanillaSDR)
  {
    gradedSceneColor.rgb = saturate(gradedSceneColor.rgb);
    gradedSceneColor.rgb = 1.0 - (shadowTint.rgb * (1.0 - gradedSceneColor.rgb));
  }
  else
  {
#if 0
    // Compress back to 1 to apply the filter. This will desaturate highlights a lot more, but also, better boost highlights that were already > 1
    float maxGradedSceneColor = max(max3(gradedSceneColor.rgb), 1.0);
    gradedSceneColor.rgb /= maxGradedSceneColor;
    gradedSceneColor.rgb = (gradedSceneColor.rgb > 0.0) ? (1.0 - (shadowTint.rgb * (1.0 - gradedSceneColor.rgb))) : gradedSceneColor.rgb;
    gradedSceneColor.rgb *= maxGradedSceneColor;
#elif 1
    // If our source color went beyond 0-1 (either because of the filter above (unlikely), or because of prior wide gamut rendering or image modulation), emulate the filter at the closest 0 or 1 edge after the edge too
    float3 clippedGradedSceneColor = 1.0 - (shadowTint.rgb * (1.0 - saturate(gradedSceneColor.rgb)));
    gradedSceneColor.rgb += clippedGradedSceneColor - saturate(gradedSceneColor.rgb);
#elif 1
    gradedSceneColor.rgb = (gradedSceneColor.rgb >= 0.0 && gradedSceneColor.rgb < 1.0) ? (1.0 - (shadowTint.rgb * (1.0 - gradedSceneColor.rgb))) : gradedSceneColor.rgb;
#else
    // If we have negative colors, flip the filter direction of the filter otherwise they'd shift towards the opposite direction.
    // Attempted idea to add negative scRGB values support, but I don't think it's needed (applying the filter through oklab might be better)
    gradedSceneColor.rgb = (gradedSceneColor.rgb < 1.0) ? ((gradedSceneColor.rgb >= 0.0) ? (1.0 - (shadowTint.rgb * (1.0 - gradedSceneColor.rgb))) : (1.0 - ((1.0 - gradedSceneColor.rgb) / shadowTint.rgb))) : gradedSceneColor.rgb;
#endif
  }

#if 0 // TEST: draw shadow tint
  o0.xyz = 1.0 / shadowTint.rgb; return;
#endif

#else // !ENABLE_LUMA

  gradedSceneColor.rgb = saturate(gradedSceneColor.rgb);
  gradedSceneColor.rgb = 1.0 - (shadowTint.rgb * (1.0 - gradedSceneColor.rgb));

#endif // ENABLE_LUMA
#endif // COLOR_GRADING && ENABLE_COLOR_GRADING

#if ENABLE_COLOR_GRADING
  gradedSceneColor.rgb = lerp(postProcessedColor.rgb, gradedSceneColor.rgb, forceVanillaSDR ? 1.0 : LumaSettings.GameSettings.ColorGradingIntensity);
#endif // ENABLE_COLOR_GRADING

  // Gamma adjustments (usually neutral, based on the user gamma, but maybe influenced by scene too)
  float3 gamma;
#if ALPHA_OUT_TYPE >= 2
  gamma = register8.xyz;
#else
  gamma = register4.xyz;
#endif
#if ENABLE_LUMA && ALPHA_OUT_TYPE >= 2 // Luma: scRGB support
  gradedSceneColor.rgb = pow(abs(gradedSceneColor.rgb), gamma.xyz * DefaultGamma) * sign(gradedSceneColor.rgb); // Concatenate linearization for tonemapping
#else
  gradedSceneColor.rgb = pow(gradedSceneColor.rgb, gamma.xyz); // No need for abs() probably, it wasn't there to begin with
#endif // ENABLE_LUMA && ALPHA_OUT_TYPE >= 2

#if ENABLE_LUMA && ALPHA_OUT_TYPE >= 2 // Skip tonemapping on world space UI

#if ENABLE_FAKE_HDR // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that (note that bloom is pre-tonemapped to avoid this blowing up)
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = LumaSettings.GameSettings.HDRBoostAmount * 0.25; // 0.1-0.15 looks good in most places. 0.2 looks better in dim scenes, but is too much AutoHDR like in bright scenes
    gradedSceneColor.rgb = FakeHDR(gradedSceneColor.rgb, normalizationPoint, fakeHDRIntensity, 0.2);
  }
#endif

  // TODO: ideally tonemapping would be after this as there's still a couple post process passes that can brighten up the image, that said... whatever...
  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  bool allowReinhard = true;
  if (forceVanillaSDR)
  {
    gradedSceneColor = saturate(gradedSceneColor);
  }
  else if (LumaSettings.DisplayMode == 1 || !allowReinhard)
  {
    bool perChannel = false; // Per channel causes hue shifts, which this game doesn't seem to expect that much, but bloom either forms steps with "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" (especially on blue, police cars sirens), or clips, with "DICE_TYPE_BY_LUMINANCE_PQ".
    DICESettings settings = DefaultDICESettings(perChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    // Darkening makes some stuff like police cars sirens bloom, become a blob of blue, because by ratio all colors were similar, so we scale them by peak, they won't be distinguishable anymore.
    // Desaturation too much also causes visible steps/blobs, but it's less worse.
    settings.DesaturationVsDarkeningRatio = 1.0;
    gradedSceneColor = DICETonemap(gradedSceneColor * paperWhite, peakWhite, settings) / paperWhite;
  }
  else
  {
#if 1
    gradedSceneColor = RestoreLuminance(gradedSceneColor, Reinhard::ReinhardRange(GetLuminance(gradedSceneColor), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
    gradedSceneColor = CorrectOutOfRangeColor(gradedSceneColor, true, true, 0.5, peakWhite / paperWhite); // TM by luminance generates out of gamut colors, and some were already in the scene anyway
#else
    gradedSceneColor = Reinhard::ReinhardRange(gradedSceneColor, MidGray, -1.0, peakWhite / paperWhite, false);
#endif
  }
  
  // The game used subtractive blends for a few things.
  // Just in case AA was disabled, otherwise it would have been filtered already.
  FixColorGradingLUTNegativeLuminance(gradedSceneColor.rgb);
  
#if UI_DRAW_TYPE == 2
  gradedSceneColor *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2
  
  gradedSceneColor = linear_to_gamma(gradedSceneColor, GCT_MIRROR);

#endif // ENABLE_LUMA && ALPHA_OUT_TYPE >= 2

  o0.xyz = gradedSceneColor.rgb;
}