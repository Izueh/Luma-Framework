#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 g_liteColour : packoffset(c0);
  float4 g_darkColour : packoffset(c1);
  float4 g_layerCloudiness : packoffset(c2);
  float4 g_layerInvFeather : packoffset(c3);
  float4 g_layerAlphas : packoffset(c4);
}

SamplerState g_densitySampler_s : register(s0);
SamplerState g_lightSampler_s : register(s1);
Texture2D<float4> g_densitySamplerTexture : register(t0);
Texture2D<float4> g_lightSamplerTexture : register(t1);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = dot(v1.xyz, v1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xw = v1.xz * r0.x;
  r0.yz = -r0.wx;
  r0.xyzw = saturate(r0.xyzw);
  r0.xyzw = r0.xyzw * r0.xyzw;
  r1.xyzw = g_lightSamplerTexture.Sample(g_lightSampler_s, v2.xy).xyzw;
  r1.xyzw = lerp(0.5, r1.xyzw, v3.x);
  r0.x = dot(r0.xyzw, r1.xyzw);
  r0.x = v3.y + r0.x;
  r0.y = g_densitySamplerTexture.Sample(g_densitySampler_s, v2.xy).y;
  r0.y = -g_layerCloudiness.x + r0.y;
  r0.z = g_layerInvFeather.x * v1.w;
  r0.y = saturate(r0.y * r0.z);
  r0.y = g_layerAlphas.x * r0.y;
  r0.z = -r0.y * v1.w + 1;
  r0.y = v1.w * r0.y;
  r0.z = r0.z * r0.z;
  r0.x = r0.z * v3.z + r0.x;
  r1.xyz = -g_darkColour.xyz + g_liteColour.xyz;
  r0.xzw = r0.x * r1.xyz + g_darkColour.xyz;
  r0.xzw = -v4.xyz + r0.xzw;
  r0.xyz = r0.y * r0.xzw + v4.xyz;
  o0.xyz = r0.xyz * v5.w + v5.xyz;
  o0.w = 1;
  
#if 0 // We can't do this because this shader draws in every single cubemap side that is used for car reflections, to square render targets
  bool forceVanillaSDR = ShouldForceSDR(v2.xy);
#else
  bool forceVanillaSDR = false;
#endif

  // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that.
  // This will also increase the brightness of reflection cubemaps, hopefully they were upgraded to float render targets too, otherwise they will clip.
  // It's better to do it here so bloom is consequently affected too, and we can apply a bigger saturation boost to the sky boxes.
  // The alternative would be to do it in the tonemapper and flag the sky pixels differently, based on stencil or depth or some alpha channel,
  // but that information doesn't seem to be safely retrievable anymore there.
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.15 * LumaSettings.GameSettings.HDRBoostIntensity;
    float fakeHDRSaturation = 0.4; // Boosting saturation in the sky just looks nice!
    o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
    o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
    
#if 0 // As much as it'd be nice to do this here, we'd need to either undo it for the final fullscreen HDR boost pass, or either way skip it for these pixels, and that doesn't seem to work reliably, given it creates steps on the image. So for now we just do the HDR boost twice for the sky pixels.
    o0.w = 2; // Set alpha to 2 to easily identify it and avoid applying the HDR boost to it!
#endif
  }
}