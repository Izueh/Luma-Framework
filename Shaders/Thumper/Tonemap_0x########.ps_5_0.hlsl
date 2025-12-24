#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

// Luma setting
#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif
#ifndef DISABLE_DISTORTION_TYPE
#define DISABLE_DISTORTION_TYPE 0
#endif
#ifndef HDR_LOOK_TYPE
#define HDR_LOOK_TYPE 1
#endif
#ifndef VANILLA_LOOK_TYPE
#define VANILLA_LOOK_TYPE 0
#endif
#ifndef BLACK_AND_WHITE
#define BLACK_AND_WHITE 0
#endif

// Offset and distortion seemengly only apply to bloom (which is actually the scene color?)
#if _0540C116 || _05C8E302 || _05E76366 || _0AF7FC85 || _0BA7CC5A || _0D88809E || _16F2F42F || _16F3B389 || _20AFCFD9 || _2AA224D0 || _33AFEAA6 || _4A0C32EA || _4CD8318D || _5C3E5427 || _68FD089B || _6908F97F || _88950D2A || _8EF8C2FA || _92927AB3 || _A12D8802 || _AF5ADD5F || _B0D713F8 || _C991064E || _F487F974 || _F9C8DD5A
#define OFFSET 1
#endif
// Always true if OFFSET is true
#if _05C8E302 || _0BA7CC5A || _20AFCFD9 || _2AA224D0 || _4A0C32EA || _4CD8318D || _68FD089B || _6908F97F || _8EF8C2FA
#define DISTORTION 1
#endif

#if _3C2C0250 || _7798668A || _B17EFCA0 || _B768EEE6 || _BE256B16 || _CE766AA8
#define BLUR 1
#endif
#if _0540C116 || _05C8E302 || _05E76366 || _0894AEAC || _0A94FD50 || _0AF7FC85 || _0BA7CC5A || _0CF4E505 || _0D88809E || _16F2F42F || _16F3B389 || _1D69DFA7 || _20AFCFD9 || _2AA224D0 || _2C11E81A || _33AF2ED3 || _33AFEAA6 || _4A0C32EA || _4A3022D9 || _4CD8318D || _56CC1C12 || _5C3E5427 || _68FD089B || _6908F97F || _6EB6C27F || _74345370 || _88950D2A || _8896B9E8 || _8EF8C2FA || _92927AB3 || _9301C9A5 || _A12D8802 || _A350A559 || _AF5ADD5F || _AFBA65C3 || _B0D713F8 || _B216143E || _C991064E || _CADBBFBB || _CDDA9276 || _E054B1AB || _E5E90689 || _E7D36C7A || _EAF1265E || _F487F974 || _F591012D || _F735A18A || _F9C8DD5A
#define BLOOM 1
#endif
// Only true if "BLOOM" is also true
// It seems like the bloom texture is actually the scene texture if there's no actual "gSceneTex" used in a permutation
#if _05C8E302 || _0A94FD50 || _0D88809E || _2C11E81A || _33AF2ED3 || _4A3022D9 || _6EB6C27F || _74345370 || _8896B9E8 || _A350A559 || _B216143E || _CADBBFBB || _CDDA9276 || _E054B1AB || _E7D36C7A || _F735A18A
#define SCENE 1
#define BLOOM_SCENE_SATURATION 1
#define BLOOM_COLOR 1
// Almost always true if "BLOOM_SCENE_SATURATION" is true
#elif _1D69DFA7
#define SCENE 1
#define BLOOM_COLOR 1
#endif

#if _0540C116 || _05C8E302 || _0BA7CC5A || _0D88809E || _16F2F42F || _16F3B389 || _20AFCFD9 || _2AA224D0 || _2C11E81A || _33AF2ED3 || _33AFEAA6 || _4A0C32EA || _4A3022D9 || _5C3E5427 || _68FD089B || _74345370 || _A12D8802 || _A350A559 || _AF5ADD5F || _B0D713F8 || _C991064E || _CADBBFBB || _CDDA9276 || _E054B1AB || _E5E90689 || _E7D36C7A || _F487F974 || _F735A18A || _F9C8DD5A
#define GAMMA 1
#endif
#if _0540C116 || _05C8E302 || _0AF7FC85 || _0BA7CC5A || _0D88809E || _16F2F42F || _16F3B389 || _20AFCFD9 || _2AA224D0 || _2C11E81A || _33AF2ED3 || _33AFEAA6 || _4A0C32EA || _4A3022D9 || _5C3E5427 || _68FD089B || _6EB6C27F || _74345370 || _88950D2A || _8EF8C2FA || _A12D8802 || _A350A559 || _AF5ADD5F || _B0D713F8 || _B216143E || _C991064E || _CADBBFBB || _CDDA9276 || _E054B1AB || _E5E90689 || _E7D36C7A || _F487F974 || _F735A18A || _F9C8DD5A
#define OUTPUT_RANGE 1
#endif
#if _0540C116 || _05C8E302 || _0AF7FC85 || _0BA7CC5A || _0D88809E || _16F2F42F || _16F3B389 || _20AFCFD9 || _2AA224D0 || _2C11E81A || _33AF2ED3 || _33AFEAA6 || _4A0C32EA || _4A3022D9 || _5C3E5427 || _68FD089B || _6EB6C27F || _74345370 || _88950D2A || _8EF8C2FA || _A12D8802 || _A350A559 || _AF5ADD5F || _B0D713F8 || _B216143E || _C991064E || _CADBBFBB || _CDDA9276 || _E054B1AB || _E5E90689 || _E7D36C7A || _F487F974 || _F735A18A || _F9C8DD5A
#define BLACK_GAMMA_XY 1
#endif
// Only true if "BLACK_GAMMA_XY" is also true
#if _0540C116 || _0BA7CC5A || _16F3B389 || _2AA224D0 || _4A3022D9 || _74345370 || _B0D713F8 || _CADBBFBB || _CDDA9276 || _F487F974
#define BLACK_GAMMA_Z 1
#endif
#if _6EB6C27F || _74345370 || _88950D2A || _8896B9E8 || _AF5ADD5F || _AFBA65C3 || _B0D713F8 || _B17EFCA0 || _B768EEE6 || _CE766AA8 || _E7D36C7A || _F735A18A || _F9C8DD5A
#define FADE 1
#endif
#if _0540C116 || _2AA224D0 || _33AF2ED3 || _5C3E5427 || _68FD089B || _74345370 || _B0D713F8 || _CADBBFBB || _CDDA9276 || _F487F974 || _F735A18A || _F9C8DD5A
#define OUTPUT_RANGE_BLACK 1
#endif
#if _7798668A || _A350A559 || _B768EEE6 || _C991064E || _CADBBFBB || _F487F974
#define INVERT_FRAC 1
#endif

// Permutation "_DB91752E" is special as it's vignette only, no scene color etc (it's probably unused)
#if _0540C116 || _05C8E302 || _0894AEAC || _0AF7FC85 || _0CF4E505 || _0D88809E || _16F2F42F || _16F3B389 || _4A0C32EA || _5C3E5427 || _6908F97F || _7798668A || _88950D2A || _92927AB3 || _A12D8802 || _AF5ADD5F || _AFBA65C3 || _B0D713F8 || _B768EEE6 || _BE256B16 || _C991064E || _CE766AA8 || _DB91752E || _E054B1AB || _EAF1265E || _F487F974 || _F9C8DD5A
#define VIGNETTE 1
#endif

#if _8081E6DD
// This permutation has nothing! It outputs black
#endif

#if !SCENE && !BLUR && !BLOOM && !_DB91752E && !_8081E6DD
#error "Unsupported permutation? There's no color to be outputted"
#endif

// Default all to off, given only hashes with the feature enabled are specified above
#ifndef OFFSET
#define OFFSET 0
#endif
#ifndef DISTORTION
#define DISTORTION 0
#endif
#ifndef SCENE
#define SCENE 0
#endif
#ifndef BLUR
#define BLUR 0
#endif
#ifndef BLOOM
#define BLOOM 0
#endif
#ifndef BLOOM_SCENE_SATURATION
#define BLOOM_SCENE_SATURATION 0
#endif
#ifndef BLOOM_COLOR
#define BLOOM_COLOR 0
#endif
#ifndef GAMMA
#define GAMMA 0
#endif
#ifndef OUTPUT_RANGE
#define OUTPUT_RANGE 0
#endif
#ifndef BLACK_GAMMA_XY
#define BLACK_GAMMA_XY 0
#endif
#ifndef BLACK_GAMMA_Z
#define BLACK_GAMMA_Z 0
#endif
#ifndef FADE
#define FADE 0
#endif
#ifndef OUTPUT_RANGE_BLACK
#define OUTPUT_RANGE_BLACK 0
#endif
#ifndef INVERT_FRAC
#define INVERT_FRAC 0
#endif
#ifndef VIGNETTE
#define VIGNETTE 0
#endif

cbuffer MultisliceConstants : register(b1)
{
  uint gSliceIndex : packoffset(c0);
  float3 _padding : packoffset(c0.y);
}

cbuffer BloomConstants : register(b3)
{
  float4 gBloomColor : packoffset(c0);
  float4 gBloomSaturation_Scene_Bloom : packoffset(c1);
}

cbuffer CubicLensConstants : register(b6)
{
  float gDistortion : packoffset(c0);
  float gCubicDistortion : packoffset(c0.y);
  float2 gOffset : packoffset(c0.z);
}

cbuffer FadeConstants : register(b8)
{
  float4 gFadeColor_Fade : packoffset(c0);
}

cbuffer InvertConstants : register(b9)
{
  float4 gInvertFrac : packoffset(c0);
}

cbuffer LevelsConstants : register(b10)
{
  float4 gBlack_InvRange_InvGamma : packoffset(c0);
  float4 gOutputRange_Black : packoffset(c1);
  float4 gBlacks : packoffset(c2);
  float4 gInvRanges : packoffset(c3);
  float4 gInvGammas : packoffset(c4);
  float4 gOutputRanges : packoffset(c5);
  float4 gOutputBlacks : packoffset(c6);
}

cbuffer RadialBlurConstants : register(b11)
{
  float4 gBlurCenter_SampleWidth : packoffset(c0);
  float4 gBlurRadiusV_MaskUHalfWidth_MaskUVRamp : packoffset(c1);
  float4 gBlurChannelWeights[12] : packoffset(c2);
  float gBlurAspect : packoffset(c14);
  float3 _blur_padding : packoffset(c14.y);
}

cbuffer VignetteConstants : register(b13)
{
  float4 gVigCenterEyes : packoffset(c0);
  float4 gCenterColor_RadiusV : packoffset(c1);
  float4 gOuterColor_Aspect : packoffset(c2);
  float4 gMaxOutputColor : packoffset(c3);
}

SamplerState gAuxSampler_s : register(s0);
SamplerState gSceneSampler_s : register(s1);
Texture2D<float4> gAuxTex : register(t0); // Bloom texture (sometimes?)
Texture2D<float4> gSceneTex : register(t1);

float3 GetLuminance_Custom(float3 color, bool forceVanilla)
{
  if (forceVanilla)
  {
    return dot(color, float3(0.3,0.59,0.11)); // Vanilla BT.601 luminance
  }
  return GetLuminance(color);
}

// Newton-Raphson Iterative Solver
float ReverseDistortion(float target = 1.0, int iterations = 5)
{
    // Start with a reasonable initial guess (usually target itself, as distortion is typically small)
    float x = target;
    [unroll]
    for (int i = 0; i < iterations; ++i)
    {
        float f = (gCubicDistortion * x * x * x * x) + (gDistortion * x * x * x) + x - target;
        float f_prime = (4.0 * gCubicDistortion * x * x * x) + (3.0 * gDistortion * x * x) + 1.0;
        // Check for near-zero derivative to prevent division by zero
        if (abs(f_prime) < 1e-6)
            break;
        // Newton-Raphson step: x_new = x - f(x) / f'(x)
        x -= f / f_prime;
    }
    return x;
}

// Global unified post process shader with all effects, mostly to do tonemapping, grading, etc
void main(
  float v0 : SV_ClipDistance0,
  float w0 : SV_CullDistance0,
  float4 v1 : SV_Position0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1;
  int4 r0i;
  
  outColor.w = 1; // Always the case

  // Adds a couple saturates etc to restore the original UNORM behaviour
  bool forceVanilla = ShouldForceSDR(w2.xy);
#if !ENABLE_LUMA
  forceVanilla = true;
#endif // !ENABLE_LUMA

  float3 tonemappedColor = 0.0;
  
#if BLUR || BLOOM // Luma: UW fix to make it look like it would at 16:9
  float2 size;
  gAuxTex.GetDimensions(size.x, size.y);
  float aspectRatio = size.x / size.y;
#endif

#if BLUR

  r0.yz = -gBlurCenter_SampleWidth.xy + v2.xy;
  r0.x = gBlurAspect * r0.y;
  r0.x = dot(r0.xz, r0.xz);
  r0.x = sqrt(r0.x);
  r0.w = 1.0 - gBlurRadiusV_MaskUHalfWidth_MaskUVRamp.x;
  r0.x = saturate(-gBlurRadiusV_MaskUHalfWidth_MaskUVRamp.x + r0.x);
  r0.x = r0.x / r0.w;
  r0.w = -gBlurRadiusV_MaskUHalfWidth_MaskUVRamp.y + abs(r0.y);
  r0.w = saturate(r0.w / gBlurRadiusV_MaskUHalfWidth_MaskUVRamp.z);
  r1.x = r0.z / gBlurRadiusV_MaskUHalfWidth_MaskUVRamp.w;
  r1.x = 1.0 - r1.x;
  r1.x = max(0, r1.x);
  r0.w = r1.x + r0.w;
  r1.x = gBlurCenter_SampleWidth.y >= v2.y;
  r1.x = r1.x ? 1.0 : 0.0; // asm: asfloat(asint(r1.x) & 0x3F800000)
  r0.w = r1.x + r0.w;
  r0.w = min(1, r0.w);
  r0.x = r0.x * r0.w;
  r0.x = gBlurCenter_SampleWidth.z * r0.x;
  float3 blurColor = 0.0;
  r0i.w = 0;
  while (r0i.w < 12)
  {
    r1.w = (int)r0i.w;
    r1.w = -r1.w * r0.x + 1;
    float2 uv = r0.yz * r1.w + gBlurCenter_SampleWidth.xy;
    float3 tempColor = gAuxTex.Sample(gAuxSampler_s, uv).xyz * gBlurChannelWeights[r0i.w].xyz;
    if (forceVanilla)
      tempColor = saturate(tempColor);
    else // Luma: fix NaNs from FLOAT RTs
    {
      tempColor = IsNaN_Strict(tempColor) ? 0.0 : tempColor;
      tempColor = IsInfinite_Strict(tempColor) ? 0.0 : tempColor;
    }
    blurColor += tempColor;
    r0i.w++;
  }

  float3 auxColor = blurColor;

#elif BLOOM

  float2 bloomUV = v2.xy;
#if DISTORTION // Distortion and offset only apply to bloom (which is actually the scene color?), they look fine in UW too

  float2 ndc = bloomUV - 0.5; // Not scaled by 2.0 as optimization I guess

#if !DISABLE_DISTORTION_TYPE

#if 0 // Luma: UW fix to make it look like it would at 16:9
  ndc.x *= max(aspectRatio / (16.0 / 9.0), 1.0);
#endif
  float ndcDistSquared = dot(ndc, ndc);
  float ndcDist = sqrt(ndcDistSquared);
  ndc *= (ndcDistSquared * ((gCubicDistortion * ndcDist) + gDistortion)) + 1.0;
  
#elif DISABLE_DISTORTION_TYPE == 2 // Stretch the pillarboxed and letterboxed image to cover the whole screen

#if 1 // Looks good. Ideally we'd just find the original rendering area size but this works too
  if ((gCubicDistortion + gDistortion) != 0.0)
    ndc *= abs(gCubicDistortion + gDistortion); // Approximate distortion reverse formula
#else // Doesn't look right, probably the math is borked
  ndc /= ReverseDistortion(1.0, 10); // Statically compiled
#endif
  
#endif // DISABLE_DISTORTION_TYPE == 0

  bloomUV = ndc + 0.5;

#endif // DISTORTION
#if OFFSET
  bloomUV += gOffset.xy;
#endif // OFFSET

  float3 bloomColor = gAuxTex.Sample(gAuxSampler_s, bloomUV).xyz;
  if (forceVanilla)
    bloomColor = saturate(bloomColor);
  else // Luma: fix NaNs from FLOAT RTs
  {
    // Probably not needed as bloom was already filtered, however this texture might not come from bloom?
    bloomColor = IsNaN_Strict(bloomColor) ? 0.0 : bloomColor;
    bloomColor = IsInfinite_Strict(bloomColor) ? 0.0 : bloomColor;
  }
#if ENABLE_LUMA && 0 // Luma: add optional clamping (Bloom might have some slight negative colors, but we now fix it at source, when it does the darkening phase, before the blurrying phase (if we did max 0 here, after blurrying, the texture would probably get darker as darkness would spread and clip more pixels, shifting the look))
  bloomColor = max(bloomColor, 0.0);
#endif // ENABLE_LUMA

#if BLOOM_COLOR
  bloomColor *= gBloomColor.xyz;
#endif // BLOOM_COLOR

  float3 auxColor = bloomColor;

#endif // BLUR || BLOOM

#if SCENE

  float3 sceneColor = gSceneTex.Sample(gSceneSampler_s, w2.xy).rgb;
  if (forceVanilla)
    sceneColor = saturate(sceneColor);
  else // Luma: fix NaNs from FLOAT RTs
  {
    sceneColor = IsNaN_Strict(sceneColor) ? 0.0 : sceneColor;
    sceneColor = IsInfinite_Strict(sceneColor) ? 0.0 : sceneColor;
  }

  tonemappedColor = sceneColor;

#if BLOOM_SCENE_SATURATION
  // Bloom saturation is actually applied on the scene texture (but only in case there's bloom!)
  bool useLumaSaturation = !forceVanilla;
  float saturation = gBloomSaturation_Scene_Bloom.x;
#if VANILLA_LOOK_TYPE >= 2
  useLumaSaturation = false;
#endif
#if HDR_LOOK_TYPE >= 2
  saturation *= forceVanilla ? 1.0 : 1.8;
#elif HDR_LOOK_TYPE >= 1
  saturation *= forceVanilla ? 1.0 : 1.666;
#endif
#if BLACK_AND_WHITE
  saturation = 0.0;
#endif
  if (!useLumaSaturation)
  {
    tonemappedColor = lerp(GetLuminance_Custom(tonemappedColor, forceVanilla), tonemappedColor, saturation); // Luma: fixed wrong luminance formula (it possibly changes the look a bit, but it should be for the best)
  }
  else // Luma: fixed saturation being done in gamma space
  {
    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    if (true) // UCS Looks better!!! Purple/blue look off otherwise
    {
        tonemappedColor = JzAzBz::rgbToJzazbz(tonemappedColor); // TODO: try the hellwig/fairchild new UCS
        tonemappedColor.yz *= saturation;
        tonemappedColor = JzAzBz::jzazbzToRgb(tonemappedColor);
    }
    else // Linear
    {
      // Empyrically found sqrt gives a closer match to vanilla (not always, and not necessarily), maybe slightly more saturated but it still looks good and better than vanilla
      tonemappedColor = Saturation(tonemappedColor, sqrt(saturation));
    }
    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
  }
#endif // BLOOM_SCENE_SATURATION

#if BLUR || BLOOM
  tonemappedColor += auxColor;
#endif // BLUR || BLOOM

#elif BLUR || BLOOM // Impossible case, but let's put it here anyway

  tonemappedColor = auxColor;

#endif // SCENE || BLUR || BLOOM

#if GAMMA
  if (forceVanilla) // Luma: disable clamping
  {
    tonemappedColor = pow(abs(tonemappedColor), gInvGammas.xyz) * sign(tonemappedColor); // Luma: added abs*sign on pow
    tonemappedColor = min(tonemappedColor, 1.0);
  }
  else // Luma: fixes highlights going wild sometimes (I tried inverting the pow direction beyond 1 but it looks worse)
  {
    tonemappedColor = (tonemappedColor < 1.0) ? (pow(abs(tonemappedColor), gInvGammas.xyz) * sign(tonemappedColor)) : tonemappedColor;
  }
#endif // GAMMA

#if OUTPUT_RANGE
  float3 vanillaRangedColor = tonemappedColor * gOutputRanges.xyz + gOutputBlacks.xyz;
#if HDR_LOOK_TYPE >= 2 // This doesn't really always look good so it's behind a flag
  if (!forceVanilla)
  {
    float3 preJab = JzAzBz::rgbToJzazbz(tonemappedColor);
    float3 rangedJAB = JzAzBz::rgbToJzazbz(vanillaRangedColor);

    // Retain the chrominance and hue of the ranged color (which might have raised blacks),
    // but restore part of the original luminance (if it's lower)
    float3 mixedJab;
    mixedJab.x = min(rangedJAB.x, lerp(preJab.x, rangedJAB.x, 0.667));
    mixedJab.yz = rangedJAB.yz;
    tonemappedColor = JzAzBz::jzazbzToRgb(mixedJab);
  }
  else
#endif
  {
    tonemappedColor = vanillaRangedColor;
  }
#endif // OUTPUT_RANGE

#if BLACK_GAMMA_XY
  tonemappedColor -= gBlack_InvRange_InvGamma.x;
  if (forceVanilla) // Luma: disable clamping
    tonemappedColor = max(tonemappedColor, 0.0);
  tonemappedColor *= gBlack_InvRange_InvGamma.y;
#endif // BLACK_GAMMA_XY
#if BLACK_GAMMA_Z
  // Note: this is seemengly only used when receiving damage, to do flashes, so it doesn't seem to need extra branches like the "GAMMA" pow above has
  tonemappedColor = pow(abs(tonemappedColor), gBlack_InvRange_InvGamma.z) * sign(tonemappedColor); // Luma: added abs*sign on pow
#endif // BLACK_GAMMA_Z

#if FADE
  tonemappedColor = lerp(tonemappedColor, gFadeColor_Fade.xyz, gFadeColor_Fade.w); // Note: if the game used this much, we could blend the fade with oklab or something to avoid raising blacks
#endif // FADE

#if OUTPUT_RANGE_BLACK
  tonemappedColor = tonemappedColor * gOutputRange_Black.x + gOutputRange_Black.y;
#endif // OUTPUT_RANGE_BLACK

#if INVERT_FRAC
  tonemappedColor += gInvertFrac.x * (tonemappedColor * -2.0 + 1.0);
#endif // INVERT_FRAC

#if (SCENE || BLUR || BLOOM) && VIGNETTE // Other permutations shouldn't be tonemapped! And probably run stacked
  // Luma: Tonemapping
  if (!forceVanilla) // TODO: absolutely verify this isn't run twice (seems like it's not, but does Vignette always run?).
  {
    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    
    if (LumaSettings.DisplayMode == 1 && 0) // This doesn't seem to be needed, the game is already plently bright and colorful
    {
      float normalizationPoint = DVS1;
      float fakeHDRIntensity = DVS2;
      float fakeHDRSaturation = DVS3;
      tonemappedColor = FakeHDR(tonemappedColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    }
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
#if VANILLA_LOOK_TYPE >= 1
    bool tonemapPerChannel = true;
#else
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1; // SDR looks only good by channel. HDR looks good with both, and by luminance clearly shows some hues that weren't intended, however... overall it looks good, we could restore some vanilla hue if ever needed
#endif
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      tonemappedColor = DICETonemap(tonemappedColor * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
#if 0 // Nice but changes SDR too much
      tonemappedColor *= 0.75; // Slightly reduce the brightness to give it more range in SDR
      float shoulderStart = 0.25;
#else
      float shoulderStart = 0.5; // Set it higher than "MidGray", otherwise it compresses too much.
#endif
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

    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
  }
#endif // SCENE || BLUR || BLOOM

#if VIGNETTE && ENABLE_VIGNETTE
  static const float4 icb[] = { { 1.000000, 0, 0, 0},
                                { 0, 1.000000, 0, 0},
                                { 0, 0, 1.000000, 0},
                                { 0, 0, 0, 1.000000} };

  // bfi r0i.x, l(31), l(1), gSliceIndex, l(1)
  uint bitmask = ((~(-1 << 31)) << 1) & 0xffffffff;
  r0i.x = ((gSliceIndex << 1) & bitmask) | (1 & ~bitmask);

  r0i.x -= 1;
  r0.z = dot(gVigCenterEyes.yw, icb[r0i.x+0].xz);
  r0i.x = gSliceIndex << 1;
  r0.y = dot(gVigCenterEyes.xz, icb[r0i.x+0].xz);
  r0.yz = w2.xy - r0.yz; // NDC vignette basically (though based around the vignette screen center)
#if 0 // Vanilla: this ended up making vignette stronger in UW, which is "wrong" and looked way too intense
  r0.x = gOuterColor_Aspect.w * r0.y;
#else // Luma: UW fix to make it look like it would at 16:9. Actually this makes it worse
  r0.x = (16.0 / 9.0) * r0.y;
#endif
  r0.x = dot(r0.xz, r0.xz);
  r0.x = sqrt(r0.x);
  float vignette = saturate(r0.x / gCenterColor_RadiusV.w);
#if SCENE || BLUR || BLOOM
  r0.xyz = lerp(gCenterColor_RadiusV.xyz, gOuterColor_Aspect.xyz, vignette);
  r1.xyz = 1.0 - r0.xyz;

  r0.xyz *= tonemappedColor * 2.0;
  r1.xyz = -((1.0 - tonemappedColor) * 2.0) * r1.xyz + 1.0; // Note: even it might look like, a saturate on the color inversion isn't needed!
  r1.xyz -= r0.xyz;
  r0.xyz += r1.xyz * ((tonemappedColor >= 0.5) ? 1.0 : 0);
  tonemappedColor = gMaxOutputColor.xyz * r0.xyz;
#else // This should be permutation "_DB91752E", and it's a simple vignette shader
  tonemappedColor = vignette;
#endif // SCENE || BLUR || BLOOM
#endif // VIGNETTE && ENABLE_VIGNETTE


  if (forceVanilla)
  {
    tonemappedColor = max(tonemappedColor, 0.0);
  }
  else
  {
#if VANILLA_LOOK_TYPE >= 3

    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    // Crop out all non supported sRGB colors (<0), emulating UNORM, very much emulating the original color
    tonemappedColor = max(tonemappedColor, 0.0);
    // Desaturate highlights as in vanilla (or well, similar to it)
    tonemappedColor = RestoreLuminance(tonemappedColor, CorrectOutOfRangeColor(tonemappedColor, false, true, 1.0));
    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);

#elif VANILLA_LOOK_TYPE >= 1

    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    // Desaturate all non supported sRGB colors (<0) // TODO: to try this more, it might look better to just do "max 0"
    tonemappedColor = CorrectOutOfRangeColor(tonemappedColor, true, false);
    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);

#elif !HDR_LOOK // This doesn't look good in HDR somehow, even if theoretically vanilla would be matched by doing max 0 (it might do now, I tested it without correcting for gamma space first)

    // At the end of every tonemap pass, clamp to BT.2020/AP0 (given there's subtractions etc), then after tonemapping, desaturate to BT.2020 if out of range
#if (SCENE || BLUR || BLOOM) && VIGNETTE
    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    tonemappedColor = BT709_To_BT2020(tonemappedColor);
    tonemappedColor = CorrectOutOfRangeColor(tonemappedColor, true, false, 0.5, 1.0, 0.2, CS_BT2020);
    tonemappedColor = BT2020_To_BT709(tonemappedColor);
    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
#else
    tonemappedColor = gamma_to_linear(tonemappedColor, GCT_MIRROR);
    tonemappedColor = BT709_To_BT2020(tonemappedColor);
    tonemappedColor = max(tonemappedColor, 0.0);
    tonemappedColor = BT2020_To_BT709(tonemappedColor); // Note: DO AP0 D65 instead
    tonemappedColor = linear_to_gamma(tonemappedColor, GCT_MIRROR);
#endif

#endif
  }

  outColor.rgb = tonemappedColor;
}