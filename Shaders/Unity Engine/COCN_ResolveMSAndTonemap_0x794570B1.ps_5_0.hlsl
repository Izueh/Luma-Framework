#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Tonemap.hlsl"

#ifndef STRETCH_ORIGINAL_TONEMAPPER
#define STRETCH_ORIGINAL_TONEMAPPER 0
#endif

Texture2DMS<float4> t0 : register(t0);

cbuffer cb0 : register(b0)
{
  float4 cb0[138];
}

// Basically identical to the full Uncharted 2 tonemapper,
// but with changed parameters and an extra division by the white scale on the input.
// 0 is mapped to 0. ~INF to ~1 or slightly more.
float3 UnityTonemapper(float3 x)
{
#if 1
  float a = 0.2;
  float b = 0.29;
  float c = 0.24;
  float d = 0.272;
  float e = 0.02;
  float f = 0.3;
  float whiteLevel = 5.3;
  float whiteScale = Tonemap_Uncharted2_Eval(whiteLevel, a, b, c, d, e, f).x;

  // Note: the addition of the first division by "whiteScale" is what makes the Unity tonemapper different from the original UC2, and that would seemengly compress colors a lot more aggressively
  return sign(x) * Tonemap_Uncharted2_Eval(abs(x) / whiteScale, a, b, c, d, e, f) / whiteScale; // Luma: add sign*abs to preserve negative values (it's fine as it outputs 0 for 0)
#else // There seemengly is no way to make this curve output something that looks right in HDR, at least not in COCOON, that heavily relied on clipping to hue shift
  float a = 0.2 * 2 * LumaSettings.DevSetting05;
  float b = 0.29 * 2 * LumaSettings.DevSetting06;
  float c = 0.24 * 2 * LumaSettings.DevSetting07;
  float d = 0.272 * 2 * LumaSettings.DevSetting08;
  float e = 0.02 * 2 * LumaSettings.DevSetting09;
  float f = 0.3 * 2 * LumaSettings.DevSetting10;
  float whiteLevel = 5.3 * 2 * LumaSettings.DevSetting03;
  float whiteScale = Tonemap_Uncharted2_Eval(whiteLevel, a, b, c, d, e, f).x;

  return sign(x) * Tonemap_Uncharted2_Eval(abs(x) / whiteScale, a, b, c, d, e, f) / (whiteScale * 2 * LumaSettings.DevSetting04);
#endif
}

// One channel only, given they are all the same
float UnityTonemapper_Inverse(float x)
{
  float a = 0.2;
  float b = 0.29;
  float c = 0.24;
  float d = 0.272;
  float e = 0.02;
  float f = 0.3;
  float whiteLevel = 5.3;
  float whiteScale = Tonemap_Uncharted2_Eval(whiteLevel, a, b, c, d, e, f).x;

  return sign(x) * Tonemap_Uncharted2_Inverse_Eval(abs(x) * whiteScale, a, b, c, d, e, f) * whiteScale;
}

// Resolve MSAA and tonemap in the meantime
// In and out in linear space (it was float textures in the vanilla game too)
void main(
  float4 v0 : SV_POSITION0,
  out float3 o0 : SV_Target0)
{
  float3 outColor = 0; // Start from 0 as then we add below
  float3 SDRColor = 0;

  const uint MSCount = asuint(cb0[134].x);
  const float exposure = cb0[137].x;

  float sourceWidth, sourceHeight, sampleCount;
  t0.GetDimensions(sourceWidth, sourceHeight, sampleCount);
  float2 uv = v0.xy / float2(sourceWidth, sourceHeight);
  bool forceSDR = ShouldForceSDR(uv, true) || LumaSettings.DisplayMode != 1;

  for (uint i = 0; i < MSCount; i++)
  {
    float3 sceneColor = t0.Load((int2)v0.xy, i).xyz;
    sceneColor *= exposure;
    float3 tonemappedSDRColor = UnityTonemapper(sceneColor); // Tonemapping before averaging the MS color is slightly better! // TODO: it's not better, and it's slow, do it after!
    float3 tonemappedColor = tonemappedSDRColor;

    static const float SDRTMMidGrayOut = MidGray; 
    static const float SDRTMMidGrayIn = UnityTonemapper_Inverse(SDRTMMidGrayOut);
    static const float SDRTMMidGrayRatio = SDRTMMidGrayOut / SDRTMMidGrayIn;
    float3 tonemappedHDRColor = sceneColor * SDRTMMidGrayRatio; // Match mid gray with the original TM output
#if !STRETCH_ORIGINAL_TONEMAPPER // Luma: restore untonemapped color from around 0.18
    tonemappedColor = lerp(tonemappedSDRColor, tonemappedHDRColor, saturate(tonemappedSDRColor / MidGray));
#elif STRETCH_ORIGINAL_TONEMAPPER // Attept at stretching the SDR tonemapper
    float powCoeff = DVS1; // <=0.5
    float startingPoint = DVS2; // MidGray
    // TODO: run in BT.2020?
    // Remap around the output's mid gray, so we keep the result "identical" below mid grey but expanded above it
    tonemappedHDRColor = (tonemappedHDRColor > startingPoint) ? (pow(tonemappedHDRColor - startingPoint + 1.0, powCoeff) + startingPoint - 1.0) : tonemappedHDRColor;
    tonemappedHDRColor /= SDRTMMidGrayRatio; // Restore back to the original/full range, to pass it to the game's SDR tonemapper again
    tonemappedHDRColor = UnityTonemapper(tonemappedHDRColor);
    // This will possibly massively increase saturation, so make sure to tonemap by channel again in HDR later
    //TODOFT: by luminance?
    tonemappedHDRColor = (tonemappedHDRColor > startingPoint) ? (pow(tonemappedHDRColor - startingPoint + 1.0, 1.0 / powCoeff) + startingPoint - 1.0) : tonemappedHDRColor;
    tonemappedColor = tonemappedHDRColor;
#endif
    outColor += forceSDR ? tonemappedSDRColor : tonemappedColor;
    SDRColor += tonemappedSDRColor;
  }
  // Normalize
  outColor /= int(MSCount);
  SDRColor /= int(MSCount);

#if !STRETCH_ORIGINAL_TONEMAPPER // Luma: restore SDR colors
  outColor = RestoreHueAndChrominance(outColor, SDRColor, 0.8, 0.4);
#endif
  o0.xyz = outColor;
}