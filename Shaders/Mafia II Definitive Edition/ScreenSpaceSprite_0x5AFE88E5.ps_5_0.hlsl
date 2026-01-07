#include "../Includes/Common.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

SamplerState GlowTexture_sampler_s : register(s0);
SamplerState ProjectiveTexture_sampler_s : register(s1);
Texture2D<float4> GlowTexture : register(t0);
Texture2D<float4> ProjectiveTexture : register(t1);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD2,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = GlowTexture.Sample(GlowTexture_sampler_s, v1.xy).x;
  r0.xyz = v2.xyz * r0.x;
  r0.w = ProjectiveTexture.Sample(ProjectiveTexture_sampler_s, w1.xy).x;
  r1.xyz = r0.xyz * r0.w;
  r0.x = r1.x + r1.y;
  o0.xyz = r1.xyz;
  r0.x = r0.z * r0.w + r0.x;
  o0.w = 0.333 * r0.x;

// These blend additively on the background, so in vanilla they'd just clip
#if ENABLE_HDR_BOOST && 1 // These seem decent even without a color boost, but are slightly better with it
  bool forceVanilla = ShouldForceSDR(v0.xy * LumaSettings.SwapchainInvSize.xy);
  if (LumaSettings.DisplayMode == 1 && !forceVanilla)
  {
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);

    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.15;
    float saturationExpansionIntensity = 0.1;
#if DEVELOPMENT && 0
    fakeHDRIntensity = DVS4;
    saturationExpansionIntensity = DVS5;
#endif
    o0.rgb = FakeHDR(o0.rgb, normalizationPoint, fakeHDRIntensity, saturationExpansionIntensity);
    
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
  }
#endif
}