#include "Includes/Common.hlsl"

SamplerState coronaTexture_s : register(s0);
Texture2D<float4> coronaTextureTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = coronaTextureTexture.Sample(coronaTexture_s, v1.xy).xyzw; // Note: if we wanted we could boost these to make them more HDR but overall it looks fine anyway
  o0.xyzw = v2.xyzw * r0.xyzw;

#if 0 // Doesn't seem to be necessary here, it makes it look worse
  bool forceVanillaSDR = ShouldForceSDR(v1.xy);
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.333 * LumaSettings.GameSettings.HDRBoostIntensity;
    float fakeHDRSaturation = 0.333;
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
    o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
  }
#endif
}