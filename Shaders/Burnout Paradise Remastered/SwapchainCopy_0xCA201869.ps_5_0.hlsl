#include "Includes/Common.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer _Globals : register(b0)
{
  float4 gv4GammaValues : packoffset(c0);
}

SamplerState Scene_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = SceneTexture.Sample(Scene_s, v1.xy).xyzw;
  // Applies the user gamma brightness value, defaults at 1
  o0.xyz = pow(abs(o0.xyz), gv4GammaValues.x) * sign(o0.xyz); // Luma: fixed support for negative values

  // Luma: tonemap the UI beyond to fix the boost fire looking weird (hue shifted and clipped) (this probably doesn't fully fix it bu whatever)
  // TODO: the turbo bar was very clipped in SDR, though for some cars, the clipped color brought it closer to the look of fire, so if we wanted to bother, we could restore a bit of clipped hue here.
  if (o0.a > 0.0)
  {
    float3 prevOutColor = o0.rgb;

    const float paperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
    o0.xyz = Reinhard::ReinhardRange(o0.xyz, MidGray, -1.0, peakWhite / paperWhite, false);
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);

    o0.xyz = lerp(prevOutColor, o0.xyz, sqrt(o0.a)); // We risk tonemapping the scene again if we don't do this
  }
}