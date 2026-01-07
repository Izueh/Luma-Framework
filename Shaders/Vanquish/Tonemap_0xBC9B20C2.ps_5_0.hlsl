#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 1
#endif

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef ENABLE_VANILLA_HIGHLIGHTS_EMULATION
#define ENABLE_VANILLA_HIGHLIGHTS_EMULATION 0
#endif

Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // Bloom
Texture2D<float4> t2 : register(t2); // 1D 256x1 LUT

SamplerState s0_s : register(s0); // Linear
SamplerState s1_s : register(s1); // Linear
SamplerState s2_s : register(s2); // Point

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  
  bool forceVanilla = ShouldForceSDR(v5.xy);

  float4 sceneColor = t0.Sample(s0_s, v5.xy).xyzw;
  if (forceVanilla)
    sceneColor = saturate(sceneColor);
  // Filtering... These don't do anything given they are set to neutral values (x&1)|0.
  // Nor they should/could do anything really, because you can't filter float values sampled from a texture with ints, unless you want to do something unorthodox.
  // It's possibly to emulate some console behaviour.
  sceneColor = asfloat(asint(sceneColor) & asint(cb3[44].xyzw));
  sceneColor = asfloat(asint(sceneColor) | asint(cb3[45].xyzw));
  
  float4 bloomColor = t1.Sample(s1_s, v5.xy).xyzw;
  if (forceVanilla) // Shouldn't be needed anyway
    bloomColor = saturate(bloomColor);
  bloomColor = asfloat(asint(bloomColor) & asint(cb3[46].xyzw));
  bloomColor = asfloat(asint(bloomColor) | asint(cb3[47].xyzw));
  bloomColor *= cb4[195].xyzw;
  bloomColor.rgb *= bloomColor.a;

  float4 postProcessedColor = float4(sceneColor.rgb, 1.0);
  
#if ENABLE_COLOR_GRADING

  float3 colorGradedColor = sceneColor.rgb;
  float3 lutEncodedInput = sceneColor.rgb;
  
#if IMPROVED_COLOR_GRADING_TYPE >= 1
  float3 lutMin = 0.0;
  float3 lutMax = 1.0;
  if (!forceVanilla)
  {
    Find1DLUTClippingEdges(t2, 256, 0.0375, 0, 0, 0, lutMin, lutMax); // TODO: this is quite expensive, move to Vertex Shader? Or just leave it here as it was kind of intended?
#if DEVELOPMENT && 0 // Print purple when LUTs present clipping (this happens!!!)
    if (any(lutMin != 0.0) || any(lutMax != 1.0))
    {
      o0 = float4(1, 0, 1, 1);
      return;
    }
#endif

    // Compress the input to be 100% within the LUT clipping range
    lutEncodedInput = remap(lutEncodedInput, 0, 1, lutMin, lutMax);
  }
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1

  // Luma: fixed LUTs using nearest sampling, which would only work if input and output were both 8bit, otherwise it quantizes
  lutEncodedInput = lutEncodedInput * (255.0 / 256.0) + (0.5 / 256.0);

  r0.xyzw = t2.Sample(s0_s, float2(lutEncodedInput.r, 0.5)).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[48].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[49].xyzw));
  colorGradedColor.r = r0.r; // TODO: why are they always taking the red output channel for all input channels? Shouldn't they take G and B too?
  r0.xyzw = t2.Sample(s0_s, float2(lutEncodedInput.g, 0.5)).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[48].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[49].xyzw));
  colorGradedColor.g = r0.r;
  r0.xyzw = t2.Sample(s0_s, float2(lutEncodedInput.b, 0.5)).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[48].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[49].xyzw));
  colorGradedColor.b = r0.r;
  
  if (!forceVanilla)
  {
#if IMPROVED_COLOR_GRADING_TYPE >= 1
    // Re-expand the LUT output from its clipping range, to the full range, preventing colors from clipping. This possibly generates a wider gamut too.
    colorGradedColor = remap(colorGradedColor, lutMin, lutMax, 0, 1);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
    
#if IMPROVED_COLOR_GRADING_TYPE >= 2 // This looks bad, it's a lot less desaturated
    // Re-apply it by luminance given that the LUT is single channel (constrast only)
    colorGradedColor = gamma_to_linear(colorGradedColor, GCT_MIRROR);
    sceneColor.rgb = gamma_to_linear(sceneColor.rgb, GCT_MIRROR);
    colorGradedColor = GetLuminance(sceneColor.rgb) != 0.0 ? (sceneColor.rgb * (GetLuminance(colorGradedColor) / GetLuminance(sceneColor.rgb))) : colorGradedColor; // Leave color in linear space
#else
    // Reproject the vanilla/clipped LUT change onto the unclamped color. This probably works totally fine, though LUT extrapolation might be better depending on the range the rendering had and how the LUTs change the image.
    colorGradedColor = saturate(sceneColor.rgb) != 0.0 ? (MultiplyExtendedGamutColor(sceneColor.rgb, colorGradedColor / saturate(sceneColor.rgb))) : colorGradedColor; // TODO: this doesn't look so good? Highlights become blue? Should we do LUT extrapolation, or maybe hue restoration from vanilla clip?
#if IMPROVED_COLOR_GRADING_TYPE >= 1
    colorGradedColor = gamma_to_linear(colorGradedColor, GCT_MIRROR); // Leave color in linear space
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2
  }

  // Desaturation (or saturation if the multiplier is negative)
#if IMPROVED_COLOR_GRADING_TYPE >= 1 // Luma: do it in linear to minimize hue shifts
  if (!forceVanilla)
  {
    float luminance = GetLuminance(colorGradedColor, GCT_POSITIVE);
    colorGradedColor += (colorGradedColor - luminance) * cb4[194].xyz;
		FixColorGradingLUTNegativeLuminance(colorGradedColor); // Make sure there's no negative numbers from the possible subtraction
    colorGradedColor = linear_to_gamma(colorGradedColor, GCT_MIRROR); // Turn back to gamma space for the code below
  }
  else
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  {
    // Luma: fixed BT.601 luminance (and doing it in gamma space)
    float luminance = GetLuminance(gamma_to_linear(colorGradedColor, GCT_POSITIVE));
    colorGradedColor += (colorGradedColor - linear_to_gamma1(luminance)) * cb4[194].xyz;
  }

  float3 levelMul = cb4[192].xyz;
  float3 levelAdd = saturate(cb4[193].xyz * ((v5.y * -cb4[193].w) + 1.0)); // TODO: should we remove the saturate here? It'd raise by more than white, but it might look good in HDR? Also should we improve the back raise?
#if IMPROVED_COLOR_GRADING_TYPE >= 1 // Luma
  if (!forceVanilla)
  {
    colorGradedColor = MultiplyExtendedGamutColor(colorGradedColor, levelMul);
    colorGradedColor = EmulateShadowOffset(colorGradedColor, levelAdd, false); // Note: this can lower the blue tint the game has (on top, visor effect), but it makes it look less old gen
  }
  else
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  {
    colorGradedColor *= levelMul;
    colorGradedColor += levelAdd;
  }

  float colorGradingIntensity = 1.0;
#if IMPROVED_COLOR_GRADING_TYPE >= 1 // Lower as it was way too extreme, crushing blacks etc
  colorGradingIntensity = 0.75;
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  postProcessedColor.rgb = lerp(postProcessedColor.rgb, colorGradedColor, colorGradingIntensity);

#endif // ENABLE_COLOR_GRADING

  postProcessedColor.rgb += bloomColor.rgb; // Note: bloom isn't color graded, but it's mostly fine given it's near white
  postProcessedColor.a = bloomColor.a; // Original code. Alpha is replaced below by Luma.

#if ENABLE_VANILLA_HIGHLIGHTS_EMULATION
  // Restore vanilla "hue" (or something close to it)
  if (!forceVanilla)
  {
    const float restorationIntensity = 0.5; // Values between 0.333 and 0.75 look good, though beyond 0.5 it starts desaturating particle effects too much and it looks lame
#if 0 // Less perceptual (looks about the same and has less chances to break though)
    postProcessedColor.rgb = lerp(postProcessedColor.rgb, RestoreLuminance(saturate(postProcessedColor.rgb), postProcessedColor.rgb), restorationIntensity);
#elif 1
    postProcessedColor.rgb = RestoreHueAndChrominance(postProcessedColor.rgb, saturate(postProcessedColor.rgb), 0.0, restorationIntensity);
#endif
  }
#endif // ENABLE_VANILLA_HIGHLIGHTS_EMULATION

  postProcessedColor *= cb4[196].xyzw; // User brightness and possibly fade to black bundled with it (the game simply clipped more when the user increased brightness...)

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

  postProcessedColor.rgb = gamma_to_linear(postProcessedColor.rgb, GCT_MIRROR);

  if (forceVanilla)
  {
    postProcessedColor.rgb = saturate(postProcessedColor.rgb); // Approximation of vanilla color
  }
  else if (LumaSettings.DisplayMode == 1)
  {
#if ENABLE_HDR_BOOST
    float normalizationPoint = 0.025;
    float fakeHDRIntensity = 0.15;
    float fakeHDRSaturation = 0.1;
    postProcessedColor.rgb = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(postProcessedColor.rgb), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif // ENABLE_HDR_BOOST

    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    settings.DesaturationVsDarkeningRatio = 1.0;
    postProcessedColor.rgb = DICETonemap(postProcessedColor.rgb * paperWhite, peakWhite, settings) / paperWhite;
  }
  else if (LumaSettings.DisplayMode == 1)
  {
#if 0
    postProcessedColor.rgb = RestoreLuminance(postProcessedColor.rgb, Reinhard::ReinhardRange(GetLuminance(postProcessedColor.rgb), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
    postProcessedColor.rgb = CorrectOutOfRangeColor(postProcessedColor.rgb, true, true, 0.5, peakWhite / paperWhite); // TM by luminance generates out of gamut colors, and some were already in the scene anyway
#else
    postProcessedColor.rgb = Reinhard::ReinhardRange(postProcessedColor.rgb, MidGray, -1.0, peakWhite / paperWhite, false);
#endif
  }
  
  float finalLuminance = linear_to_gamma1(GetLuminance(postProcessedColor.rgb), GCT_POSITIVE);

  postProcessedColor.rgb = linear_to_gamma(postProcessedColor.rgb, GCT_MIRROR);

  o0.xyzw = postProcessedColor;
  o0.a = saturate(o0.a); // Luma: force saturate to emulate UNORM

#if 1 // Luma: used by our FXAA
  o0.a = finalLuminance;
#endif
}