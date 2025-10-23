#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

// There are 64 permutations of the tonemap shader in INSIDE (7 branches, with some combinations skipped). Some of them are possibly unused, but still, all of them were available in the dumps.
// The pause menu brightness preview has a separate permutation ("BLACK_BARS"), the death screen another one ("DARKEN"), under water is another ("WAVE_EFFECT"), lens distortion+chromatic aberration, etc etc.
// They all share a unique hex value in the code that can quickly identify them "0x7ef311c3".
// See the code for more information about these.
// 0x1926C2D3 is the most barebones permutation while 0xBF930E1F is the most complete one.
#define LOW_QUALITY 0
// Only ever false if "LOW_QUALITY" is true.
#define COLOR_FILTER_TEXTURE 0
// Only ever true true if "COLOR_FILTER_TEXTURE" is true.
#define WAVE_EFFECT 0
#define DESATURATION 0
#define USER_BRIGHTNESS 0
#define BLACK_BARS 0
#define DARKEN 0

// 32x
#if _06132AC1 || _07FE2DEC || _0EF00A11 || _1926C2D3 || _21B3A200 || _30074255 || _3796FF82 || _38A7430E || _48E38F85 || _493DA507 || _91970ABF || _9B7D1702 || _B0398871 || _B5908835 || _C71FE0A4 || _D4C38351 || _02C7E7CB || _ED517C58 || _18010638 || _49BDA2EC || _50873049 || _51DF35B3 || _59C674F6 || _6792E8D3 || _746E571C || _8847F08D || _9D055A64 || _A51DAE54 || _A97B7480 || _C753F2E4 || _FF2021BF || _3A63AE73
#undef LOW_QUALITY
#define LOW_QUALITY 1
#endif

// 48x
#if _02C7E7CB || _0AE21975 || _10E5A1DF || _18010638 || _2337502D || _2D6B78F6 || _2FE2C060 || _343CD73C || _3A63AE73 || _3E3A55F7 || _4030784C || _49BDA2EC || _50873049 || _515B88D8 || _519DF6E7 || _51DF35B3 || _59C674F6 || _6787B520 || _6792E8D3 || _6956455B || _7003995F || _746E571C || _82A02335 || _84F1D7F4 || _8589AC8E || _87E4E17A || _8847F08D || _8DEE69CB || _90337E76 || _9D055A64 || _9D414A70 || _A51DAE54 || _A5777313 || _A97B7480 || _BA96FA20 || _BEC46939 || _BF930E1F || _BFAB5215 || _C4065BE1 || _C5DABDD4 || _C753F2E4 || _CF97AAD6 || _D6763B69 || _D86D3CA9 || _D8EE0CED || _ED517C58 || _F0503978 || _FF2021BF
#undef COLOR_FILTER_TEXTURE
#define COLOR_FILTER_TEXTURE 1
#endif

// 32x
#if _10E5A1DF || _2337502D || _343CD73C || _4030784C || _515B88D8 || _519DF6E7 || _6956455B || _84F1D7F4 || _87E4E17A || _9D414A70 || _BF930E1F || _BFAB5215 || _C5DABDD4 || _CF97AAD6 || _D6763B69 || _D8EE0CED || _02C7E7CB || _18010638 || _3A63AE73 || _49BDA2EC || _50873049 || _51DF35B3 || _59C674F6 || _6792E8D3 || _746E571C || _8847F08D || _9D055A64 || _A51DAE54 || _A97B7480 || _C753F2E4 || _ED517C58 || _FF2021BF
#undef WAVE_EFFECT
#define WAVE_EFFECT 1
#endif

// 32x
#if _02C7E7CB || _06132AC1 || _0AE21975 || _2337502D || _2D6B78F6 || _3796FF82 || _3A63AE73 || _3E3A55F7 || _4030784C || _48E38F85 || _493DA507 || _50873049 || _519DF6E7 || _6792E8D3 || _6956455B || _7003995F || _8589AC8E || _91970ABF || _9B7D1702 || _9D414A70 || _A51DAE54 || _A5777313 || _B5908835 || _BA96FA20 || _BF930E1F || _BFAB5215 || _C5DABDD4 || _C71FE0A4 || _C753F2E4 || _D86D3CA9 || _ED517C58 || _FF2021BF
#undef DESATURATION
#define DESATURATION 1
#endif

// 32x
#if _02C7E7CB || _06132AC1 || _07FE2DEC || _0EF00A11 || _10E5A1DF || _18010638 || _2FE2C060 || _3E3A55F7 || _4030784C || _48E38F85 || _515B88D8 || _519DF6E7 || _6787B520 || _746E571C || _8589AC8E || _87E4E17A || _8847F08D || _90337E76 || _9D055A64 || _9D414A70 || _A51DAE54 || _A5777313 || _B0398871 || _B5908835 || _BA96FA20 || _BF930E1F || _C4065BE1 || _C71FE0A4 || _C753F2E4 || _D4C38351 || _D8EE0CED || _ED517C58
#undef USER_BRIGHTNESS
#define USER_BRIGHTNESS 1
#endif

// 32x
#if _06132AC1 || _07FE2DEC || _0AE21975 || _10E5A1DF || _18010638 || _21B3A200 || _343CD73C || _38A7430E || _3A63AE73 || _493DA507 || _49BDA2EC || _6787B520 || _6956455B || _7003995F || _746E571C || _82A02335 || _8589AC8E || _87E4E17A || _8DEE69CB || _90337E76 || _9B7D1702 || _9D414A70 || _A5777313 || _A97B7480 || _B5908835 || _BF930E1F || _BFAB5215 || _C753F2E4 || _CF97AAD6 || _D4C38351 || _ED517C58 || _FF2021BF
#undef BLACK_BARS
#define BLACK_BARS 1
#endif

// 32x
#if _02C7E7CB || _06132AC1 || _10E5A1DF || _2337502D || _30074255 || _3796FF82 || _38A7430E || _3A63AE73 || _3E3A55F7 || _4030784C || _48E38F85 || _493DA507 || _49BDA2EC || _515B88D8 || _59C674F6 || _6787B520 || _6792E8D3 || _6956455B || _7003995F || _746E571C || _82A02335 || _84F1D7F4 || _8589AC8E || _9D055A64 || _B0398871 || _BF930E1F || _C4065BE1 || _CF97AAD6 || _D4C38351 || _D86D3CA9 || _ED517C58 || _F0503978
#undef DARKEN
#define DARKEN 1
#endif

#if COLOR_FILTER_TEXTURE
Texture2D<float4> bloomTexture : register(t4); // Very blurred
Texture2D<float4> sceneTexture : register(t3); // TAA'd
Texture2D<float4> colorFilterTexture : register(t2); // RGB color palette/filter. 3x2
#else
Texture2D<float4> bloomTexture : register(t3);
Texture2D<float4> sceneTexture : register(t2);
#endif
Texture2D<float4> noiseTexture : register(t1); // 256x256
Texture2D<float4> lensDistortionAndChromaticAberrationTexture : register(t0); // Matches your aspect ratio at its lower resolution (e.g. 16x9, 32x9), given it's generated in the previous pass

#if COLOR_FILTER_TEXTURE
SamplerState lensDistortionAndChromaticAberrationSampler : register(s4);
SamplerState noiseSampler : register(s3);
SamplerState bloomSampler : register(s2);
SamplerState colorFilterSampler : register(s1);
#else
SamplerState lensDistortionAndChromaticAberrationSampler : register(s3);
SamplerState noiseSampler : register(s2);
SamplerState bloomSampler : register(s1);
#endif
SamplerState sceneSampler : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[14];
}

// Weird to do this in shaders, it seems slower than just doing a sqrt? Maybe that wasn't the intent?
float3 Quake_rsqrt(float3 number)
{
    int3 i = asint(number);                  // reinterpret float bits as int
    i = 0x5F375A86 - (i >> 1);               // the magic number bit hack (in Quake it was 0x5F3759DF)
    float3 y = asfloat(i);                   // reinterpret back to float
    y = y * (1.5f - 0.5f * number * y * y);  // 1 Newton-Raphson iteration
    return y;
}

// Alpha is emissiveness, or anyway, "brightness" boost.
// This is one of the ways they managed to make the game have an HDR look even with UNORM textures.
// TODO: mess around with these to get a more HDR look? The tonemapper already seems to do a lot.
float GetSceneWeightFromAlpha(float sceneColorAlpha)
{
  float sceneWeight = max(sceneColorAlpha, 0.0); // Luma: add max 0 on alpha just to make sure texture upgrades didn't make the value go below 0
  sceneWeight += 0.629960537;
  sceneWeight *= sceneWeight;
  sceneWeight *= sceneWeight;
  sceneWeight += 0.842509866;
  return sceneWeight;
}

float3 AdjustBloom(float3 bloomColor)
{
#if ENABLE_LUMA
  return sqr_mirrored(bloomColor); // Luma: added mirroring to preserve scRGB colors
#else // !ENABLE_LUMA
  return sqr(saturate(bloomColor));
#endif // ENABLE_LUMA
}

// Quick test to enable/disable Luma changes
float3 OptionalSaturate(float3 x)
{
#if ENABLE_LUMA
  return x;
#else // !ENABLE_LUMA
  return saturate(x);
#endif // ENABLE_LUMA
}

float3 ApplyColorFilter(float3 color, float3 colorFilter)
{
#if ENABLE_LUMA // Mirror the rgb scaling in the opposite direction. Like... if red was meant to be doubled, scaling its negative value by 2 will actually halve it
  return (color >= 0.0 || colorFilter <= FLT_EPSILON) ? (color * colorFilter) : (color / colorFilter);
#else // !ENABLE_LUMA
  return color * colorFilter;
#endif // ENABLE_LUMA
}

// Cubic polynomial
// This actually seems to take a log encoded color and linearize it, outputting what would be the final gamma 2.2 image
float3 ApplyCustomCurve(float3 color, float levelMultiplication, float levelAdditive, float4 polynomialParams, float midtonesHDRBoost = 0.0, float highlightsHDRBoost = 0.0)
{
  // Params:
  // x: highlights intensity  (directly proportional) (can be 0, and defaults to it, changes if the user changes the brightness)
  // y: midtones and highlights intensity (directly proportional) (can be 0, and defaults to it, changes if the user changes the brightness) (this also slightly lowers blacks when raised, acting as contrast)
  // z: subtractive color (higher means darker, on the whole range, similar to pow) (best left untouched, it cold crush and clip blacks if changed)
  // w: additive color (higher means brighter, usually 0)
  // levelAdditive: raises blacks a bit, but if set to 0, it can crush them, so it's likely compensated by the other params (e.g. z and "levelAdditive"?).
  
#if ENABLE_LUMA && 1
  // Changing these will automatically output images in the HDR range!
  // Note that these are already changed by the user brightness setting so that's best left at default. It doesn't seem like these are ever changed depending on the game scene.
  if (LumaSettings.DisplayMode == 1) // HDR mode, leave SDR as it was
  {
    polynomialParams.x += highlightsHDRBoost; // Raise highlights (we could afford to go a bit higher too, but it looks too much in some scenes, AutoHDR like). Setting this to negative values can break the image.
    polynomialParams.y += midtonesHDRBoost; // Raise midtones and highlights. It also crushes blacks a bit, acting as contrast.
  }
#elif DEVELOPMENT && 0
  polynomialParams.x += LumaSettings.DevSetting06 * 2.0;
  polynomialParams.y += LumaSettings.DevSetting07 * 2.0;
#elif DEVELOPMENT && 0
  levelMultiplication *= 1.0 - LumaSettings.DevSetting04;
  levelAdditive *= 1.0 - LumaSettings.DevSetting05;
  polynomialParams.x *= 1.0 - LumaSettings.DevSetting06;
  polynomialParams.y *= 1.0 - LumaSettings.DevSetting07;
  polynomialParams.z *= 1.0 - LumaSettings.DevSetting08;
  polynomialParams.w *= 1.0 - LumaSettings.DevSetting09;
#endif

  float3 c1 = OptionalSaturate(color * levelMultiplication + levelAdditive); // Multiply and add (first) // Luma: removed saturate

#if DESATURATION // It's seemengly some color filter, or desaturation
  float3 scaledC = cb0[3].rgb * c1;
  float2 rg_rb = scaledC.rr + scaledC.gb;
  float filterOffset = (c1.b * cb0[3].b) + rg_rb.x + (sqrt(scaledC.g * rg_rb.y) * 2.0 * cb0[3].a);
  c1 -= filterOffset;
  c1 *= cb0[13].x;
  c1 += filterOffset;
#endif

  float3 c2 = (c1 * polynomialParams.x) + polynomialParams.y; // Multiply and add (second)

  float3 c3 = (c1 * c2) + polynomialParams.z;

  return (c1 * c3) + polynomialParams.w;
}

// Luma: added support for scRGB (negative colors), though given that the curve might not return 0 for 0, and thus already shift the sign, we need to carefully work around that
// Note that this original TM might have actually expected to receive negative values due to the code that came just before it generating some, so it's not 100% guaranteed this wrapped formula is good in all cases,
// but I couldn't spot any negative effects of doing this, also, all negative values in the og tonemapper would likely get lost to multiplications by itself and additions.
// One thing to note though is that possibly the input color here was encoded in "log" space, thus can't have values beyond 0-1? That doesn't seem to be the case as they are preserved fine.
float3 ApplyCustomCurveWrapped(float3 color, float levelMultiplication, float levelAdditive, float4 polynomialParams, float midtonesHDRBoost = 0.0, float highlightsHDRBoost = 0.0)
{
  const float3 zeroShift = ApplyCustomCurve(0.0, levelMultiplication, levelAdditive, polynomialParams, midtonesHDRBoost, highlightsHDRBoost);
  float3 colorSigns = sign(color);
  color = ApplyCustomCurve(abs(color), levelMultiplication, levelAdditive, polynomialParams, midtonesHDRBoost, highlightsHDRBoost);
  color -= zeroShift;
  color *= colorSigns;
  color += zeroShift;
  return color;
}

float3 LinearizeLog2(float3 v, float minL, float maxL)
{
    float ratio = (maxL + minL) / minL;
    return minL * pow(ratio, v) - minL;
}

// Applies bloom and some minor color filtering, and lens distortion
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  nointerpolation float4 v2 : TEXCOORD1,
#if !USER_BRIGHTNESS
  nointerpolation float2 v3 : TEXCOORD2,
#else
  nointerpolation float3 v3 : TEXCOORD2,
#endif
  out float4 outColor : SV_Target0)
{
  outColor.w = 1;

  const float fadeToWhite = cb0[11].x; // White at 1
  const float colorFilterIntensity = cb0[6].z; // Neutral (unfiltered) at 0. Color filtering isn't always used
  const float blackFloor = cb0[10].x;
  const float colorPow = cb0[10].z;
#if !USER_BRIGHTNESS
  const float userBrightness = 1.0;
#else
  // Black at 0 (could also be vignette (exclusively based on the horizontal axis), though that's not used (at least not in every scene))
  // Update: this is likely to be the user brightness (if the brightness is >= default), as it's not in the shader if the user lowers the brightness
  const float userBrightness = v3.z;
#endif
  float midtonesHDRBoost = 0.5 * LumaSettings.GameSettings.HDRIntensity;
  float highlightsHDRBoost = 1.0 * (LumaSettings.GameSettings.HDRIntensity > 1.0 ? sqrt(LumaSettings.GameSettings.HDRIntensity) : LumaSettings.GameSettings.HDRIntensity); // Looks good like this

	float screenWidth, screenHeight;
	sceneTexture.GetDimensions(screenWidth, screenHeight);

  // Start from the "center"
#if LOW_QUALITY
  const int numIterations = 3;
#else
  const int numIterations = 8;
#endif
  const float uvStep = 1.0 / float(numIterations);

  const float lensDistortionScale = 42.5; // Probably a manually picked constant, not the size of the texture, the same variable is found in the generation shader.
  float4 lensDistortionAndChromaticAberration = lensDistortionAndChromaticAberrationTexture.SampleLevel(lensDistortionAndChromaticAberrationSampler, v1.xy, 0).xyzw;
  float2 lensDistortion = (lensDistortionAndChromaticAberration.xy / lensDistortionScale) - (0.5 / lensDistortionScale); // Outwards lens distortion. Neutral at 0.
  float2 chromaticAberration = (lensDistortionAndChromaticAberration.zw * (0.5 / lensDistortionScale)) - (0.25 / lensDistortionScale); // Chromatic aberration + inwards lens distortion. Neutral at 0.

#if !ENABLE_LENS_DISTORTION
  lensDistortion = 0;
#elif ENABLE_LUMA // Fix lens distortion being stronger on the sides for Ultrawide
  const float sourceAspectRatio = screenWidth / screenHeight; // Not exactly the size of the screen, but of the current viewport anyway (at least the source one, this texture might add black bars, but probably doesn't, they are added later if the screen aspect ratio is different from the render resolution)
  const float originalAspectRatio = 16.0 / 9.0;
  float2 ndc = (v1 - 0.5) * 2.0;
  float aspectRatiosRatio = sourceAspectRatio / originalAspectRatio;
  float lensDistortionHorizontalPow = 0.666; // Manually found value to match UW lens distortion with the 16:9 look
  lensDistortion.x = pow(abs(lensDistortion.x), pow(lerp(1.0, aspectRatiosRatio, sqr(abs(ndc.x))), lensDistortionHorizontalPow)) * sign(lensDistortion.x);
#endif
#if !ENABLE_CHROMATIC_ABERRATION
  chromaticAberration = 0;
#endif

  const float2 noiseRandomOffset = cb0[9].yz; // Slides it across the screen (it's too fast at high frame rates... noise becomes hard to see)
  const float2 noiseTextureInvSize = cb0[8].xy; // 256x256
  float2 noiseUV = v0.xy + noiseRandomOffset; // This is per pixel and is properly scaled by screen aspect ratio
  noiseUV *= noiseTextureInvSize;
  // TODO: is this actually intended to be film grain or is it noise/dither? In that case, we should use "ENABLE_DITHER" instead, and possibly not scale it by resolution
#if ENABLE_LUMA // Fix grain being near invisible at 4k, set it to 1080, the most common resolution of when the game shipped
  noiseUV *= 1080.0 / screenHeight;
#endif // ENABLE_LUMA
  float4 noise = noiseTexture.SampleLevel(noiseSampler, noiseUV, 0).rgba;

#if ENABLE_FILM_GRAIN
#if ENABLE_LUMA // Fixed film grain raising blacks (see "blackFloorScale" above if you change this, given that had the opposite effect, both were possibly somehow intentional)
  noise.rgb = noise.rgb * 4.0 - 2.0;
  noise.rgb *= 5.0 / 4.0; // Slightly increase the intensity to kinda match the previous range
#else // !ENABLE_LUMA
  noise.rgb = noise.rgb * 5.0 - 2.0; // Not exactly centered, this raises blacks!
#endif // ENABLE_LUMA
  noise.rgb /= 219.0; // Random scale that probably looked good
#else // !ENABLE_FILM_GRAIN
  noise.rgb = 0.0;
#endif // ENABLE_FILM_GRAIN

  // The palette/scene samples starting shift (between 0 and 1 (or more))
  float colorSampleShift = 0.5;
#if WAVE_EFFECT // Usually happens under water, presumably to create a wave like effect
  colorSampleShift = noise.a;
#endif

  float2 currentSceneUV = v1.xy + lensDistortion;
  currentSceneUV += chromaticAberration * uvStep * colorSampleShift;
  float2 currentColorPaletteUV = float2(uvStep * colorSampleShift, 0.5); // This will do all horizontal samples from 0.0625 to 0.9375, as if the texture had an orizontal size 0f 8, though it doesn't, but it's fine anyway (8 is our horizontal iterations)

  // First iteration
  // We called it center as supposedly it's not offsetted, the other ones go in the same (positive?) direction though, not in both the opposite directions
#if COLOR_FILTER_TEXTURE
  float3 centralColorFilter = colorFilterTexture.SampleLevel(colorFilterSampler, currentColorPaletteUV, 0).rgb;
  centralColorFilter = lerp(1.0, centralColorFilter, colorFilterIntensity);
#else
  float alternativeColorFilter = 1.0 - cb0[6].z; // "colorFilterIntensity" is being re-used for a different purpose (I think)
  float3 centralColorFilter = float3(1.0, alternativeColorFilter.xx); // GB (R neutral)
#endif
  float4 centralSceneColor = sceneTexture.Sample(sceneSampler, currentSceneUV).rgba;
#if 0 // Test: passthrough color (a lot dimmer!)
  outColor = centralSceneColor; return;
#endif
  centralSceneColor.rgb *= GetSceneWeightFromAlpha(centralSceneColor.a);
  float3 centralBloomColor = bloomTexture.Sample(bloomSampler, currentSceneUV).rgb;
  centralBloomColor = AdjustBloom(centralBloomColor);
  
  float3 sceneColor = ApplyColorFilter(centralSceneColor.rgb, centralColorFilter);
  float3 bloomColor = ApplyColorFilter(centralBloomColor, centralColorFilter);
  float3 colorFilter = centralColorFilter;

  // Other n-1 iterations.
  // This does some blurring and chromatic aberration.
#if LOW_QUALITY // That's how it was in the original shaders
  [unroll]
#endif
  for (int i = 1; i < numIterations; i++)
  {
    currentColorPaletteUV.x += uvStep;
    currentSceneUV += chromaticAberration * uvStep;
    
#if COLOR_FILTER_TEXTURE
    float3 currentColorFilter = colorFilterTexture.SampleLevel(colorFilterSampler, currentColorPaletteUV, 0).rgb; // Point sampler, likely on purpose
    currentColorFilter = lerp(1.0, currentColorFilter, colorFilterIntensity);
#else // "LOW_QUALITY" is implied here
    float3 currentColorFilter = 1.0;
    if (i == 1)
      currentColorFilter.rb = alternativeColorFilter; // RB (G neutral)
    else if (i == 2)
      currentColorFilter.rg = alternativeColorFilter; // RG (B neutral)
#endif
    colorFilter += currentColorFilter;

    float4 currentSceneColor = sceneTexture.Sample(sceneSampler, currentSceneUV).rgba;
    currentSceneColor.rgb *= GetSceneWeightFromAlpha(currentSceneColor.a);
    sceneColor += currentSceneColor.rgb * currentColorFilter;
    
    float3 currentBloomColor = bloomTexture.Sample(bloomSampler, currentSceneUV).rgb;
    currentBloomColor = AdjustBloom(currentBloomColor);
    bloomColor += currentBloomColor * currentColorFilter;
  }
#if 0 // Test: passthrough color (a lot dimmer)
  outColor.rgb = sceneColor / float(numIterations); return;
#endif

#if 1
  // Some kind of hacky performance optimization (pow/sqrt like) to do brightness scaling... The image is overly dark without it
  int3 hackyMathResultA = 0x7ef311c3 - asint(colorFilter); // This shouldn't have any negative values
  float3 hackyMathResultB = 2.0 - (asfloat(hackyMathResultA) * colorFilter);
  float3 finalColorFilter = hackyMathResultB * asfloat(hackyMathResultA); // You'd expect this to be "1.0 / numIterations", but it's not!
#else // Cheaper and more accurate equivalent, however it shifts the look too much
  float finalColorFilter = 1.0 / numIterations;
#endif

  float3 scaledSceneColor = OptionalSaturate(ApplyColorFilter(sceneColor, finalColorFilter)); // Luma: removed saturate
  float3 scaledBloomColor = OptionalSaturate(ApplyColorFilter(bloomColor, finalColorFilter)); // Luma: removed saturate
  float3 invertedScaledSceneColor = 1.0 - scaledSceneColor;
  float3 invertedScaledBloomColor = 1.0 - scaledBloomColor;
  // Compose bloom
#if ENABLE_LUMA // Luma: added abs*sign to preserve negative colors, though we can't restore two signs or we'd turn positive again, so we only restore the primary one for now (ideally we'd find the influence of each of the two and pick an average of the two signs? Or do this in a wider color space, but the bloom here is so blurred that it has little influence)
  float3 invertedScaledComposedColor = abs(invertedScaledBloomColor) * abs(invertedScaledSceneColor) * sign(invertedScaledSceneColor); // TODO: make sure that restoring only one of the original signs here makes sense, as in case only one of the two was negative, it might create a step in colors? It's fine!
#else // !ENABLE_LUMA
  float3 invertedScaledComposedColor = invertedScaledBloomColor * invertedScaledSceneColor; 
#endif // ENABLE_LUMA
  float3 scaledComposedColor = 1.0 - invertedScaledComposedColor;

#if ENABLE_LUMA && ENABLE_BLACK_FLOOR_TWEAKS_TYPE >= 1 // Luma: replace the clipped min black + black crush, with a lerp to black, given it killed near black detail (this lowers brightness usually). There's a few scenes where this was meant to hide detail, but overall it's not nice. It's possibly it was done to hide dithering near black

#if DEVELOPMENT && 1 // Draw green if it's negative, as it'd raise brightness
  if (blackFloor < 0)
  {
    outColor.rgb = float3(0, 1, 0); return;
  }
#endif

#if ENABLE_BLACK_FLOOR_TWEAKS_TYPE <= 2 // Allow boosting visibility by skipping darkening
  // Empyrically found value that keeps the blacks level roughly the same without clipping it (note that this is calibrated to also match the original black level with the additive film grain). Values like 5 would be a better match in some scenes, but they'd completely destroy other scenes.
  const float blackFloorScale = 2.0;
#if 1 // New improved version (we also tried doing it by luminance and preserving the clipped hue, but it makes little sense, by channel is best)
  scaledComposedColor = (blackFloor < FLT_EPSILON) ? max(scaledComposedColor - blackFloor, min(scaledComposedColor, 0.0)) : max(scaledComposedColor - (blackFloor * saturate(scaledComposedColor / (blackFloor * blackFloorScale))), min(scaledComposedColor, 0.0)); // Don't allow it to go lower the possible negative value it already had, to avoid artifacts
#else // This version posterized too much
  float3 blackFloorCorrectionRatio = (blackFloor < FLT_EPSILON) ? 1.0 : sqr(saturate(scaledComposedColor / (blackFloor * blackFloorScale))); // This will ignore a negative black floor that would raise the whole image
  scaledComposedColor = lerp(0.0, scaledComposedColor, blackFloorCorrectionRatio);
#endif
#endif // ENABLE_BLACK_FLOOR_TWEAKS_TYPE <= 2

#else

  scaledComposedColor = max(scaledComposedColor - blackFloor, 0.0);

#endif // ENABLE_LUMA && ENABLE_BLACK_FLOOR_TWEAKS_TYPE >= 1
  scaledComposedColor /= cb0[10].y - blackFloor; // Raises brightness usually

#if ENABLE_LUMA // Luma: added abs*sign to preserve negative colors (the alternaitve would have caused NaNs in SDR too anyway)
  scaledComposedColor = pow(abs(scaledComposedColor), colorPow) * sign(scaledComposedColor); // Lowers brightness usually
#else // !ENABLE_LUMA
  scaledComposedColor = pow(scaledComposedColor, colorPow); // Lowers brightness usually
#endif // ENABLE_LUMA

#if ENABLE_LUMA && ENABLE_BLACK_FLOOR_TWEAKS_TYPE == 2 // Luma: anchor the black floor and only raise above it. This isn't really necessary as blacks were already crushed anyway so raising the black floor is usually fine
  scaledComposedColor = lerp(scaledComposedColor, 1.0, fadeToWhite * saturate(scaledComposedColor)); // TODO: make sure this is never used to do fades to white, but I don't think so.
#else
  scaledComposedColor = lerp(scaledComposedColor, 1.0, fadeToWhite); // Raises brightness and black floor when active
#endif // ENABLE_LUMA && ENABLE_BLACK_FLOOR_TWEAKS_TYPE == 2
#if DEVELOPMENT && 0 // Draw purple if it's used
  if (fadeToWhite != 0)
  {
    outColor.rgb = float3(1, 0, 1); return;
  }
#endif
  
#if 1 // Some kind of brightness scaling... It adds a lot of contrast and flattens highlights
  float3 someColor1 = 1.0 - scaledComposedColor;

#if ENABLE_LUMA && 0 // Luma: high quality version, plus mirroring to support scRGB colors. This looks quite different
  float3 someColor2 = sqr_mirrored(someColor1) + (0.2 * cb0[10].w); // Luma: added mirroring to preserve scRGB colors
  float3 sqrtColor = sqrt(abs(someColor2)) * sign(someColor2);
  someColor1 -= safeDivision(someColor2, sqrtColor, 0);
#elif ENABLE_LUMA // Luma: Conserve the hacky optimized square root, but make it work in scRGB
  float3 someColor2 = sqr(max(someColor1, 0.0)) + (0.2 * cb0[10].w);
  float3 invSqrtColor = Quake_rsqrt(someColor2);
  someColor1 -= someColor2 * invSqrtColor;
#else // !ENABLE_LUMA
  float3 someColor2 = sqr(someColor1) + (0.2 * cb0[10].w);
  float3 invSqrtColor = Quake_rsqrt(someColor2);
  someColor1 -= someColor2 * invSqrtColor;
#endif // ENABLE_LUMA

  scaledComposedColor += someColor1 * 0.5; // Note: instead of generating invalid (negative) colors here, we could do a dynamic/smart shadow curve, however the tonemapper seems to rely on negative values too?
#endif

  // The main tonemapping
  // It seemengly darkens the game usually, and extracts a lot of highlights from the already dim image, given that the range was mostly within SDR even if textures were upgraded
#if ENABLE_LUMA
  bool forceSDR = ShouldForceSDR(v1.xy, true); // Not a full "TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL" implementation, but it's mostly there
  if (forceSDR)
  {
    midtonesHDRBoost = 0.0;
    highlightsHDRBoost = 0.0;
  }

  bool TM_BT2020 = LumaSettings.DisplayMode == 1 && !forceSDR; // It seems to look a bit better, usually it barely makes any difference
#if 1 // TODO: disabled, this currently makes no sense and causes posterization. Before applying the curve, the color is log encoded, though we don't know the encoding! Note: we could try something like "LinearizeLog2()" above
  TM_BT2020 = false;
#endif
  const bool TM_ByLuminance = false;
  float TM_ByLuminance_Amount_Shadow = 1.0;
  float TM_ByLuminance_Amount_Highlights = 1.0;
  const bool desaturateInLinear = true; // Slightly more accurate in most cases
  float maxDesaturation = LumaSettings.GameSettings.HighlightsDesaturation;
  bool blackAndWhite = false;
  bool fixNegativeLuminance = false; // This messes up the tonemapper curve below, that apparently expects negative luminances, to be restored with an additive offset etc

  if (TM_BT2020 || fixNegativeLuminance)
  {
    scaledComposedColor = gamma_to_linear(scaledComposedColor, GCT_MIRROR);
    if (fixNegativeLuminance)
      FixColorGradingLUTNegativeLuminance(scaledComposedColor); // Remove negative luminances, given there's plenty in the shadow, due to the "Quake_rsqrt" code above. Note that this seemengly raises the black floor a lot, given that most of it was negative.
    scaledComposedColor = TM_BT2020 ? BT709_To_BT2020(scaledComposedColor) : scaledComposedColor;
    scaledComposedColor = linear_to_gamma(scaledComposedColor, GCT_MIRROR);
  }

  float3 tonemapByChannel = ApplyCustomCurveWrapped(scaledComposedColor, v3.y, v3.x, v2, midtonesHDRBoost, highlightsHDRBoost);
  if (!TM_ByLuminance && !blackAndWhite)
  {
    scaledComposedColor = tonemapByChannel;
    
    // In HDR, desaturate highlights to get a look closer to SDR (HDR tonemapper output is more saturated due to the more aggressive params)
    if (LumaSettings.DisplayMode == 1 && !forceSDR)
    {
      float highlightsBoost = (midtonesHDRBoost + highlightsHDRBoost) / 2.0; // These are usually both in 0-1 range and rougly boost saturation equally
      // Values closer to 0.5 provide a look more similar to SDR, but then HDR would end up looking like SDR. 0.333 can desaturate too little in a few scenes that feel too "joyful" for the game, but most of the scene textures are already a grey scale,
      // so this rarely matters, and it allows for a few colorful things to shine through HDR.
      float desaturation = maxDesaturation * highlightsBoost;

      scaledComposedColor = desaturateInLinear ? gamma_to_linear(scaledComposedColor, GCT_MIRROR) : scaledComposedColor;
      float saturation = 1.0 - saturate(GetLuminance(scaledComposedColor, TM_BT2020 ? CS_BT2020 : CS_BT709) - (desaturateInLinear ? MidGray : 0.5)) * saturate(desaturation);
      scaledComposedColor = Saturation(scaledComposedColor, saturation, TM_BT2020 ? CS_BT2020 : CS_BT709); // TODO: desat with oklab or something?
      scaledComposedColor = desaturateInLinear ? linear_to_gamma(scaledComposedColor, GCT_MIRROR) : scaledComposedColor;
    }
    
    if (TM_BT2020)
    {
      // Note: we could skip some gamma conversions through the desaturation branch, but whatever
      scaledComposedColor = gamma_to_linear(scaledComposedColor, GCT_MIRROR);

      // Needed to avoid occasional red botches on dark texels when entering the water. Each of these seem to help and play a part in fixing it (the actual reason is not clear, it could be an inf, denorm or nan), it only happens in BT.2020
      // the issue is triggerable in the "Clockwork - Shadows at noon" orb location, when entering and exiting the water
      outColor.rgb = (IsNaN_Strict(outColor.rgb) || IsInfinite_Strict(outColor.rgb)) ? 0.0 : outColor.rgb;
	    FixColorGradingLUTNegativeLuminance(outColor.rgb, 1, CS_BT2020);
      scaledComposedColor = max(scaledComposedColor, 0.0); // Clip gamut beyond BT.2020, it's all "random" anyway

      scaledComposedColor = linear_to_gamma(BT2020_To_BT709(scaledComposedColor), GCT_MIRROR);
    }
#if 0 // Test: passthrough color
    outColor.rgb = scaledComposedColor.rgb; return;
#endif
  }
  else
  {
    float scaledComposedColorLuminance = linear_to_gamma1(GetLuminance(gamma_to_linear(scaledComposedColor, GCT_MIRROR), TM_BT2020 ? CS_BT2020 : CS_BT709), GCT_MIRROR);
    float3 tonemapByLuminance = ApplyCustomCurveWrapped(scaledComposedColorLuminance, v3.y, v3.x, v2, midtonesHDRBoost, highlightsHDRBoost); // TODO: This might also loose the color filtering the tonemapper applies...
    float tonemapByLuminanceAverage = GetLuminance(gamma_to_linear(tonemapByLuminance, GCT_MIRROR), TM_BT2020 ? CS_BT2020 : CS_BT709); // Just in case rgb were different

    // Just because... it looks cool in this game
    if (blackAndWhite)
    {
      scaledComposedColor = linear_to_gamma1(max(tonemapByLuminanceAverage, 0.0));
      // No need to convert from BT.2020 to BT.709 given that luminance (grey scale) is the same
    }
    else
    {
      float TM_ByLuminance_Amount = lerp(TM_ByLuminance_Amount_Shadow, TM_ByLuminance_Amount_Highlights, linear_to_gamma1(saturate(tonemapByLuminanceAverage)));
      tonemapByChannel = gamma_to_linear(tonemapByChannel, GCT_MIRROR);
      scaledComposedColor = gamma_to_linear(scaledComposedColor, GCT_MIRROR);
      // TODO: this just won't work... it denormalizes floats or anyway causes NaNs, no matter how we change the math or check against NaNs...
      scaledComposedColor = lerp(tonemapByChannel, RestoreLuminance(scaledComposedColor, tonemapByLuminanceAverage, true, TM_BT2020 ? CS_BT2020 : CS_BT709), TM_ByLuminance_Amount);
      scaledComposedColor = TM_BT2020 ? BT2020_To_BT709(scaledComposedColor) : scaledComposedColor;
      scaledComposedColor = linear_to_gamma(scaledComposedColor, GCT_MIRROR);
    }
  }
#else // !ENABLE_LUMA
  scaledComposedColor = ApplyCustomCurve(scaledComposedColor, v3.y, v3.x, v2);
#endif // ENABLE_LUMA

  float3 otherFilter = 1.0;
#if BLACK_BARS
  float barsColor = (v1.y > cb0[12].w) ? cb0[12].y : cb0[12].z;
  otherFilter = (1.0 - barsColor) * float3(0.0980392173, 0.121568628, 0.141176477) + barsColor;
#endif

  outColor.rgb = scaledComposedColor * userBrightness * otherFilter;

#if ENABLE_LUMA // Luma: HDR display mapping and UI etc
  outColor.rgb = gamma_to_linear(outColor.rgb, GCT_MIRROR);
  
#if 1 // This might slightly change a lot of colors due to the game's strong dithering
	FixColorGradingLUTNegativeLuminance(outColor.rgb);
#endif

#if ENABLE_FAKE_HDR // Not really needed anymore after tweaking the original tonemapper
  float normalizationPoint = 0.25; // Found empyrically (could be improved)
  float fakeHDRIntensity = 0.5;
  float boostSaturation = 0.0;
  outColor.rgb = FakeHDR(outColor.rgb, normalizationPoint, fakeHDRIntensity, boostSaturation);
#endif // ENABLE_FAKE_HDR

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

  DICESettings settings = DefaultDICESettings();
#if 0 // Since we boosted the SDR tonemapper to produce an HDR image, we can benefit from extra desaturation as it increases it a bit too much for the mood of the game
  settings.Type = DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE; // There's not much HDR range in this game so don't add extra hue shifts with tonemapping by channel
#endif
#if 0 // Disabled as it makes highlights weaker
  settings.ShoulderStart = paperWhite / peakWhite; // Only tonemap beyond paper white, so we leave the SDR range untouched (roughly)
#endif
  outColor.rgb = DICETonemap(outColor.rgb * paperWhite, peakWhite, settings) / paperWhite;

#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
  outColor.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif

  outColor.rgb = linear_to_gamma(outColor.rgb, GCT_MIRROR);
#else
#if UI_DRAW_TYPE == 2
  outColor.rgb *= pow(LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits, 1.0 / DefaultGamma);
#endif
#endif // ENABLE_LUMA

#if DARKEN // Death permutations
  // Darken screen, or anyway fade to black. For consistency, we do this after the HDR tonemapping.
  outColor.rgb *= cb0[12].x;
#endif

  // Add dither/grain at the very end, how it already was. This will generate some invalid luminances near zero but whatever.
  outColor.rgb += noise.rgb;
}