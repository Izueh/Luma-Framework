#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer cbConstants : register(b1)
{
  float4 Constants : packoffset(c0);
}

SamplerState Texture0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xy = Constants.xy + v1.xy;
  o0.xyzw = Texture0.Sample(Texture0_s, r0.xy).xyzw;
  
#if 1 // Luma
  bool doHDR = LumaSettings.DisplayMode == 1;
  // We need to tonemap the background copy as it's done before the final swapchain copy shader, which is when we would have tonemapped
  if (doHDR)
  {
#if 1
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    o0.rgb = DICETonemap(o0.rgb * paperWhite, peakWhite, settings) / paperWhite;
#else // Optionally fully clamp to SDR
    o0.rgb = Reinhard::ReinhardSimple(o0.rgb);
#endif
  }

  // fix gamma not matching on UI due to Vanilla swapping between sRGB and non sRGB views (we can't in HDR)
  o0.xyz = linear_to_sRGB_gamma(o0.xyz, GCT_MIRROR);
#endif
}