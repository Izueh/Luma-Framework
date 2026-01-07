#include "Includes/Common.hlsl"

SamplerState ColorTextureMapY_s : register(s0);
SamplerState ColorTextureMapCbCr_s : register(s1);
Texture2D<float4> ColorTextureMapYTexture : register(t0);
Texture2D<float4> ColorTextureMapCbCrTexture : register(t1);

// Plays and decodes videos
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = ColorTextureMapYTexture.Sample(ColorTextureMapY_s, v3.xy).x;
  r0.yz = ColorTextureMapCbCrTexture.Sample(ColorTextureMapCbCr_s, v3.xy).xy;
#if 1 // Luma: fix videos being decoded as BT.601 (limited range), instead of BT.709 (limited range)
  r1.xyz = YUVtoRGB(r0.x, r0.z, r0.y, 1);
#else
  r0.xyz = float3(-0.0627449974,-0.50195998,-0.50195998) + r0.xyz;
  r1.x = dot(r0.xyz, float3(1.1641444,-0.0017889,1.59578621));
  r1.y = dot(r0.xyz, float3(1.1641444,-0.391442806,-0.813482106));
  r1.z = dot(r0.xyz, float3(1.1641444,2.0178256,-0.00124580006));
#endif
  r1.w = 1;
  r0.xyzw = r1.xyzw * v2.xyzw + v1.xyzw;

  // Luma: add a light AutoHDR pass on videos
  if (LumaSettings.DisplayMode == 1)
  {
    r0.rgb = gamma_to_linear(r0.rgb, GCT_MIRROR);
    r0.rgb = PumboAutoHDR(r0.rgb, lerp(sRGB_WhiteLevelNits, 250.0, LumaSettings.GameSettings.HDRBoostIntensity), LumaSettings.UIPaperWhiteNits);
    r0.rgb = linear_to_gamma(r0.rgb, GCT_MIRROR);
  }

  // Black bars (random?)
  r1.x = (0.985 < v3.y);
  r1.y = (v3.y < 0.015);
  r1.x = asfloat(asint(r1.y) | asint(r1.x));
  o0.xyz = r1.x ? 0.0 : r0.xyz;

  o0.w = r0.w;
}