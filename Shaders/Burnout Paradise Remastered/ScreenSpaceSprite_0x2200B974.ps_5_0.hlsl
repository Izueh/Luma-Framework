#include "Includes/Common.hlsl"

SamplerState coronaTexture_s : register(s0);
Texture2D<float4> coronaTextureTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 coronaColor = coronaTextureTexture.Sample(coronaTexture_s, v1.xy).xyzw; // Note: if we wanted we could boost these to make them more HDR but overall it looks fine anyway

  float2 uv = v0.xy * LumaSettings.GameSettings.InvRenderRes;
  bool forceVanillaSDR = ShouldForceSDR(uv);

#if ENABLE_IMPROVED_BLOOM // Removes the ugly, old gen looking, halo around coronas
  if (!forceVanillaSDR)
  {
    float powerFactor = 2.0; // 3 also looks good sometimes but it's a bit too much, and increases saturation too much?
    coronaColor.w = pow(coronaColor.w, powerFactor); // It seems like alpha was always 1, probably cuz the source is BC1 so alpha is just 1 bit
    coronaColor.rgb = pow(coronaColor.rgb, powerFactor);
  }
#endif

  o0.xyzw = v2.xyzw * coronaColor.xyzw;

#if ENABLE_IMPROVED_BLOOM && 1 // Doesn't seem to be that necessary here, but let's do it, these lights need to be visible for gameplay reasons
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.4 * LumaSettings.GameSettings.HDRBoostIntensity;
    float fakeHDRSaturation = 0.4;
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
    o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
  }
  else if (LumaSettings.DisplayMode != 1 && !forceVanillaSDR) // Fixed boost for SDR
  {
    o0.xyz *= 1.25;
  }
#endif
}