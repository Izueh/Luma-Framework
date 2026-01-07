#include "../Includes/Common.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

cbuffer _Globals : register(b0)
{
  float4 c130_GlobalSceneParams : packoffset(c15);
  float4 C002_MaterialColor : packoffset(c64);
  float2 D013_SpecularPowerAndLevel : packoffset(c65);
  float D142_SkyboxBlendRatio : packoffset(c66);
  float D350_ForcedWorldNormalZ : packoffset(c67);
  float4 c025_VisualColorModulator : packoffset(c99);
}

SamplerState S000_DiffuseTexture_sampler_s : register(s8);
Texture2D<float4> S000_DiffuseTexture : register(t8);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = v1.xy * float2(1,0.5) + float2(0,0.5);
  r0.xyzw = S000_DiffuseTexture.Sample(S000_DiffuseTexture_sampler_s, r0.xy).xyzw;
  r1.xy = float2(1,0.5) * v1.xy;
  r1.xyzw = S000_DiffuseTexture.Sample(S000_DiffuseTexture_sampler_s, r1.xy).xyzw;
  r0.xyzw = -r1.xyzw + r0.xyzw;
  r0.xyzw = D142_SkyboxBlendRatio * r0.xyzw + r1.xyzw;
  r1.xyz = C002_MaterialColor.xyz + -r0.xyz;
  r0.xyz = C002_MaterialColor.www * r1.xyz + r0.xyz;
  r0.xyzw = c025_VisualColorModulator.xyzw * r0.xyzw;
  r0.xyzw = max(float4(0,0,0,0), r0.xyzw);
  o0.w = min(1, r0.w);
  o0.xyz = r0.xyz;
    
#if ENABLE_HDR_BOOST && 0 // The sky seems fine... disabled
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