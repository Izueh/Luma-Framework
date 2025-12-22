#include "Includes/Common.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer _Params : register(b0)
{
  float4 register0 : packoffset(c0);
}

SamplerState sampler0_s : register(s0);
SamplerState sampler1_s : register(s1);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);

// Draws purely additively on background
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyz = texture0.Sample(sampler0_s, v1.xy).xyz;
  r1.xyz = texture1.Sample(sampler1_s, v1.xy).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  o0.xyz = register0.x * r0.xyz;
  o0.w = 0;

#if ENABLE_LUMA // TODO: sometimes bloom is drawn as additive through the Copy shader, and in that case, we aren't handling it, because we can't easily know if it was bloom or any other copy
#if ENABLE_FAKE_HDR // Apply the inverse of our following HDR boost to avoid bloom going crazy in it
  bool forceVanillaSDR = ShouldForceSDR(v1.xy);
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRReduction = 0.5; // Found empyrically
    float bloomRelativeScale = 0.75; // How bright is bloom compared to the background, on overage
    float fakeHDRIntensity = -LumaSettings.GameSettings.HDRBoostAmount * 0.25 * fakeHDRReduction;
    o0.xyz = FakeHDR(o0.xyz * bloomRelativeScale, normalizationPoint, fakeHDRIntensity, 0.2) / bloomRelativeScale;
  }
#endif
#if 0 // Optionally pre-tonemap to 1 (in gamma space) to avoid bloom going beyond the SDR level, however it'd look clipped. At best we could pre-tonemap it, but it doesn't seem to be necessary anyway
  o0.xyz = Reinhard::ReinhardRange(o0.xyz, 0.5, -1.0, 1.0, false); // Tonemap to 1 in gamma space
#endif
#endif // ENABLE_LUMA

  o0.xyz *= LumaSettings.GameSettings.BloomAndLensFlareIntensity; // Scale in gamma space

#if !ENABLE_POST_PROCESS_EFFECTS
  o0.xyz = 0;
#endif
}