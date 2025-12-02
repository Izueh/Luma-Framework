#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer cbSSAA : register(b5)
{
  float4 g_Offset : packoffset(c0);
}

SamplerState SamplerGenericBilinearClamp_s : register(s13);
Texture2D<float4> colorBuffer : register(t0);

// TODO: 0x8D82CD42, 0x41D3CF49 and 0xD3440029 use a more approximate FXAA implementation that does improper luminance calculations, though they don't seem to be used?
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  r0.xy = float2(0.5,0.5) * g_Offset.xy;
  r0.xy = g_Offset.xy * v0.xy + -r0.xy;
  r0.zw = g_Offset.xy * float2(1,0) + r0.xy;
  r1.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r0.zw, 0).xyzw;
  r0.z = GetLuminance(r1.xyz); // Luma: fixed BT.601 luminance
  r1.xy = g_Offset.xy + r0.xy;
  r1.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r1.xy, 0).xyzw;
  r0.w = GetLuminance(r1.xyz);
  r2.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r0.xy, 0).xyzw;
  r0.xy = g_Offset.xy * float2(0,1) + r0.xy;
  r3.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r0.xy, 0).xyzw;
  r0.x = GetLuminance(r3.xyz);
  r0.y = GetLuminance(r2.xyz);
  r1.xy = r0.zy + r0.wx;
  r1.yw = r1.yy + -r1.xx;
  r2.xy = r0.yx + r0.zw;
  r2.y = r2.x + -r2.y;
  r2.x = r2.x + r0.x;
  r2.x = r2.x + r0.w;
  r2.x = 0.03125 * r2.x;
  r2.x = max(0.0078125, r2.x);
  r2.z = min(abs(r2.y), abs(r1.w));
  r1.xz = -r2.yy;
  r2.x = r2.z + r2.x;
  r2.x = 1 / r2.x;
  r1.xyzw = r2.xxxx * r1.xyzw;
  r1.xyzw = max(float4(-8,-8,-8,-8), r1.xyzw);
  r1.xyzw = min(float4(8,8,8,8), r1.xyzw);
  r1.xyzw = g_Offset.xyxy * r1.xyzw;
  r2.xyzw = float4(-0.5,-0.5,0.5,0.5) * r1.xyzw;
  r1.xyzw = float4(-0.166666672,-0.166666672,0.166666672,0.166666672) * r1.zwzw;
  r2.xy = g_Offset.xy * v0.xy + r2.xy;
  r2.zw = g_Offset.xy * v0.xy + r2.zw;
  r3.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r2.zw, 0).xyzw;
  r2.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r2.xy, 0).xyzw;
  r2.xyz = r2.xyz + r3.xyz;
  r2.xyz = float3(0.25,0.25,0.25) * r2.xyz;
  r1.xy = g_Offset.xy * v0.xy + r1.xy;
  r1.zw = g_Offset.xy * v0.xy + r1.zw;
  r3.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r1.zw, 0).xyzw;
  r1.xyzw = colorBuffer.SampleLevel(SamplerGenericBilinearClamp_s, r1.xy, 0).xyzw;
  r1.xyz = r1.xyz + r3.xyz;
  r2.xyz = r1.xyz * float3(0.25,0.25,0.25) + r2.xyz;
  r1.xyz = float3(0.5,0.5,0.5) * r1.xyz;
  r1.w = GetLuminance(r2.xyz);
  r2.w = min(r0.x, r0.w);
  r0.x = max(r0.x, r0.w);
  r0.w = min(r0.y, r0.z);
  r0.y = max(r0.y, r0.z);
  r0.x = max(r0.y, r0.x);
  r0.y = min(r0.w, r2.w);
  r3.xy = (int2)v0.xy;
  r3.zw = float2(0,0);
  r3.xyzw = colorBuffer.Load(r3.xyz).xyzw;
  r0.z = GetLuminance(r3.xyz);
  o0.w = r3.w;
  r0.y = min(r0.z, r0.y);
  r0.x = max(r0.z, r0.x);
  r0.x = (r0.x < r1.w);
  r0.y = (r1.w < r0.y);
  r0.x = asfloat(asint(r0.x) | asint(r0.y));
  o0.xyz = r0.x ? r1.xyz : r2.xyz;

#if 1 // Luma: Tonemapping
  // Tonemapper doesn't always run so tonemap here if we detected that (the switch might be 1 frame late)
  if (LumaData.CustomData1 > 0)
  {
    if (LumaSettings.DisplayMode == 1)
    {
      float normalizationPoint = 0.02; // Found empyrically
      float fakeHDRIntensity = 0.2;
      float fakeHDRSaturation = 0.2;
      o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    }

    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1; // Vanilla clipped (hue shifted) look is better preserved with this
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      o0.xyz = DICETonemap(o0.xyz * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
      float shoulderStart = 0.333; // Set it higher than "MidGray", otherwise it compresses too much.
      if (tonemapPerChannel)
      {
        o0.xyz = Reinhard::ReinhardRange(o0.xyz, shoulderStart, -1.0, peakWhite / paperWhite, false);
      }
      else
      {
        o0.xyz = RestoreLuminance(o0.xyz, Reinhard::ReinhardRange(GetLuminance(o0.xyz), shoulderStart, -1.0, peakWhite / paperWhite, false).x, true);
        o0.xyz = CorrectOutOfRangeColor(o0.xyz, true, true, 0.5, 0.5, peakWhite / paperWhite);
      }
    }

  #if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
  #endif
  }
#endif
}