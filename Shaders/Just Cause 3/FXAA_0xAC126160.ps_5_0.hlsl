#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/DICE.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

cbuffer cbConsts : register(b1)
{
  float4 Consts[4] : packoffset(c0);
}

SamplerState Samp_s : register(s0);
Texture2D<float4> Tex : register(t0);

#define cmp

// FXAA (gamma space in and out)
// This also copies on the swapchain
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  int4 r6i;
  r0.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(0, -1)).xyz;
  r1.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(-1, 0)).xyz;
  r2.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(0, 0)).xyz; // Central sample
  r3.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(1, 0)).xyz;
  r4.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(0, 1)).xyz;
  r0.w = GetLuminance(r0.xyz); // Luma: fixed approximate luma calculations (note that we still do them in gamma space, which isn't correct...)
  r1.w = GetLuminance(r1.xyz);
  r2.w = GetLuminance(r2.xyz);
  r3.w = GetLuminance(r3.xyz);
  r4.w = GetLuminance(r4.xyz);
  r5.x = min(r1.w, r0.w);
  r5.y = min(r4.w, r3.w);
  r5.x = min(r5.x, r5.y);
  r5.x = min(r5.x, r2.w);
  r5.y = max(r1.w, r0.w);
  r5.z = max(r4.w, r3.w);
  r5.y = max(r5.y, r5.z);
  r5.y = max(r5.y, r2.w);
  r5.x = r5.y + -r5.x;
  r5.y = 0.125 * r5.y;
  r5.y = max(0.0416666679, r5.y);
  r5.y = cmp(r5.x >= r5.y);
  if (r5.y != 0) {
    r6.xy = saturate(float2(4,4) * Consts[0].zw);
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = r0.xyz + r2.xyz;
    r0.xyz = r0.xyz + r3.xyz;
    r0.xyz = r0.xyz + r4.xyz;
    r1.x = r1.w + r0.w;
    r1.x = r1.x + r3.w;
    r1.x = r1.x + r4.w;
    r1.x = r1.x * 0.25 + -r2.w;
    r1.x = abs(r1.x) / r5.x;
    r1.x = -0.25 + r1.x;
    r1.x = max(0, r1.x);
    r1.x = 1.33333337 * r1.x;
    r1.x = min(0.75, r1.x);
    r3.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(-1, -1)).xyz;
    r4.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(1, -1)).xyz;
    r5.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(-1, 1)).xyz;
    r7.xyz = Tex.SampleLevel(Samp_s, v1.xy, 0, int2(1, 1)).xyz;
    r8.xyz = r4.xyz + r3.xyz;
    r8.xyz = r8.xyz + r5.xyz;
    r8.xyz = r8.xyz + r7.xyz;
    r0.xyz = r8.xyz + r0.xyz;
    r0.xyz = r0.xyz * r1.xxx;
    r1.y = GetLuminance(r3.xyz);
    r1.z = GetLuminance(r4.xyz);
    r3.x = GetLuminance(r5.xyz);
    r3.y = GetLuminance(r7.xyz);
    r3.z = -0.5 * r0.w;
    r3.z = r1.y * 0.25 + r3.z;
    r3.z = r1.z * 0.25 + r3.z;
    r4.x = -0.5 * r1.w;
    r4.y = r1.w * 0.5 + -r2.w;
    r4.z = -0.5 * r3.w;
    r4.y = r3.w * 0.5 + r4.y;
    r3.z = abs(r4.y) + abs(r3.z);
    r4.y = -0.5 * r4.w;
    r4.y = r3.x * 0.25 + r4.y;
    r4.y = r3.y * 0.25 + r4.y;
    r3.z = abs(r4.y) + r3.z;
    r1.y = r1.y * 0.25 + r4.x;
    r1.y = r3.x * 0.25 + r1.y;
    r3.x = r0.w * 0.5 + -r2.w;
    r3.x = r4.w * 0.5 + r3.x;
    r1.y = abs(r3.x) + abs(r1.y);
    r1.z = r1.z * 0.25 + r4.z;
    r1.z = r3.y * 0.25 + r1.z;
    r1.y = r1.y + abs(r1.z);
    r1.y = cmp(r1.y >= r3.z);
    r1.z = r1.y ? -r6.y : -r6.x;
    r0.w = r1.y ? r0.w : r1.w;
    r1.w = r1.y ? r4.w : r3.w;
    r3.x = r0.w + -r2.w;
    r3.y = r1.w + -r2.w;
    r0.w = r0.w + r2.w;
    r0.w = 0.5 * r0.w;
    r1.w = r1.w + r2.w;
    r1.w = 0.5 * r1.w;
    r3.z = cmp(abs(r3.x) >= abs(r3.y));
    r0.w = r3.z ? r0.w : r1.w;
    r1.w = max(abs(r3.x), abs(r3.y));
    r1.z = r3.z ? r1.z : -r1.z;
    r3.x = 0.5 * r1.z;
    r3.y = r1.y ? 0 : r3.x;
    r3.x = asfloat(asint(r1.y) & asint(r3.x));
    r4.xy = v1.xy + r3.yx;
    r1.w = 0.25 * r1.w;
    r6.z = 0;
    r3.xy = r1.yy ? r6.xz : r6.zy;
    r3.zw = r4.xy + -r3.xy;
    r4.xy = r4.xy + r3.xy;
    r4.zw = r3.zw;
    r5.xy = r4.xy;
    r5.zw = r0.ww;
    r6.xyz = float3(0,0,0);
    r6i.z = 0;
    while (r6i.z < 16) {
      if (r6.x == 0) {
        float3 col = Tex.SampleLevel(Samp_s, r4.zw, 0).rgb;
        r6.w = GetLuminance(col);
      } else {
        r6.w = r5.z;
      }
      if (r6.y == 0) {
        float3 col = Tex.SampleLevel(Samp_s, r5.xy, 0).rgb;
        r7.x = GetLuminance(col);
      } else {
        r7.x = r5.w;
      }
      r7.y = r6.w + -r0.w;
      r7.y = cmp(abs(r7.y) >= r1.w);
      r6.x = asfloat(asint(r6.x) | asint(r7.y));
      r7.y = r7.x + -r0.w;
      r7.y = cmp(abs(r7.y) >= r1.w);
      r6.y = asfloat(asint(r6.y) | asint(r7.y));
      r7.y = asfloat(asint(r6.y) & asint(r6.x));
      if (r7.y != 0) {
        r5.z = r6.w;
        r5.w = r7.x;
        break;
      }
      r7.yz = r4.zw + -r3.xy;
      r4.zw = r6.xx ? r4.zw : r7.yz;
      r7.yz = r5.xy + r3.xy;
      r5.xy = r6.yy ? r5.xy : r7.yz;
      r6i.z++;
      r5.z = r6.w;
      r5.w = r7.x;
    }
    r3.xy = v1.xy + -r4.zw;
    r1.w = r1.y ? r3.x : r3.y;
    r3.xy = -v1.xy + r5.xy;
    r3.x = r1.y ? r3.x : r3.y;
    r3.y = cmp(r1.w < r3.x);
    r3.y = r3.y ? r5.z : r5.w;
    r2.w = r2.w + -r0.w;
    r2.w = cmp(r2.w < 0);
    r0.w = r3.y + -r0.w;
    r0.w = cmp(r0.w < 0);
    r0.w = cmp(asint(r2.w) == asint(r0.w));
    r0.w = r0.w ? 0 : r1.z;
    r1.z = r3.x + r1.w;
    r1.w = min(r3.x, r1.w);
    r1.z = -1 / r1.z;
    r1.z = r1.w * r1.z + 0.5;
    r0.w = r1.z * r0.w;
    r1.z = r1.y ? 0 : r0.w;
    r3.x = v1.x + r1.z;
    r0.w = asfloat(asint(r0.w) & asint(r1.y));
    r3.y = v1.y + r0.w;
    r1.yzw = Tex.SampleLevel(Samp_s, r3.xy, 0).xyz;
    r0.xyz = r0.xyz * float3(0.111111112,0.111111112,0.111111112) + r1.yzw;
    r2.xyz = -r1.x * r1.yzw + r0.xyz;
  }
  outColor.xyz = r2.xyz;
  outColor.w = 0;
  
#if 1 // Duplicate from the swapchain copy shader
  bool doHDR = !ShouldForceSDR(v1.xy) && LumaSettings.DisplayMode == 1;
  if (doHDR)
  {
#if ENABLE_HDR_BOOST
    float normalizationPoint = 0.025;
    float fakeHDRIntensity = 0.1;
    float fakeHDRSaturation = LumaSettings.GameSettings.HDRBoostSaturationAmount;
    outColor.rgb = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(outColor.rgb), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif
      
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    outColor.rgb = DICETonemap(outColor.rgb * paperWhite, peakWhite, settings) / paperWhite;
  }

#if UI_DRAW_TYPE == 2
	ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, VANILLA_ENCODING_TYPE, min(GAMMA_CORRECTION_TYPE, 1), true);
  outColor.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, min(GAMMA_CORRECTION_TYPE, 1), VANILLA_ENCODING_TYPE, true);
#endif

  outColor.xyz = linear_to_sRGB_gamma(outColor.xyz, GCT_MIRROR);
#endif
}