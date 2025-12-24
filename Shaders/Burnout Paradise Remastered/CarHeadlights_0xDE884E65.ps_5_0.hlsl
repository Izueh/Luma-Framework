#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 g_depthConversion : packoffset(c0);
  float4 g_headlightConstants : packoffset(c1);
  row_major float4x4 g_clipToHeadlight : packoffset(c2);
}

SamplerState g_headlightConeSampler_s : register(s0);
SamplerState g_depthSampler_s : register(s1);
Texture2D<float4> g_headlightConeSamplerTexture : register(t0);
Texture2D<float4> g_depthSamplerTexture : register(t1);

// Screen space "additive" lighting on top of the final scene
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyz = v1.xyw / v1.w;
  r1.xyzw = g_clipToHeadlight._m10_m11_m12_m13 * r0.y;
  r1.xyzw = r0.x * g_clipToHeadlight._m00_m01_m02_m03 + r1.xyzw;
  r0.xy = r0.xy * float2(0.5,-0.5) + 0.5;
  r0.x = g_depthSamplerTexture.Sample(g_depthSampler_s, r0.xy).x;
  r1.xyzw = r0.x * g_clipToHeadlight._m20_m21_m22_m23 + r1.xyzw;
  r0.xyzw = r0.z * g_clipToHeadlight._m30_m31_m32_m33 + r1.xyzw;
  r1.xyz = (abs(r0.xyz) < abs(r0.w));
  r0.xyz = r0.xyz / r0.w;
  r0.xyz += 1.0;
  // This creates a weird jitter pattern?
  r0.w = asfloat(asint(r1.y) & asint(r1.x));
  r0.w = asfloat(asint(r1.z) & asint(r0.w));
  r0.w = asfloat(asint(r0.w) & 0x3f800000); // x ? 1.0 : 0.0
  r0.z = -r0.z * 0.5 + 1;
  r0.xy = 0.5 * r0.xy;
  r1.xyzw = g_headlightConeSamplerTexture.Sample(g_headlightConeSampler_s, r0.xy).xyzw;
  r0.x = min(g_headlightConstants.x, r0.z);
  r0.x = r0.x * r0.w;
  r0.x = g_depthConversion.w * r0.x;
  o0.w = r1.w * r0.x;
  o0.xyz = r1.xyz;
#if 1 // Slightly boost headlights at night // TODO: does this cover all cars that have them? We need to
  float2 uv = v0.xy * LumaSettings.GameSettings.InvRenderRes;
  bool forceVanillaSDR = ShouldForceSDR(uv);
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
    o0.xyz *= 1.0 + LumaSettings.GameSettings.HDRBoostIntensity * 0.333;
#endif
}