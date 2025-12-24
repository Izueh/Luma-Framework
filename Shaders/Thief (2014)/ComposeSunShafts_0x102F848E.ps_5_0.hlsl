#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Shafts
Texture2D<float4> t1 : register(t1); // Scene

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb0 : register(b0)
{
  float4 cb0[21];
}

void main(
  float2 v0 : TEXCOORD0,
  float2 w0 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = -v0.xy * cb0[11].zw + cb0[5].xy;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = 0.5 * r0.x;
  r0.x = min(1, r0.x);
  r0.y = -cb0[12].w * 0.5 + 1.5;
  r0.y = -cb0[12].w + r0.y;
  r1.xyzw = t0.Sample(s1_s, v0.xy).xyzw;
#if 1 // Luma: protection
  r1.w = saturate(r1.w);
#endif
  r0.z = r1.w * r1.w;
  r1.xyz *= cb0[13].xyz;
  r0.y = r0.z * r0.y + cb0[12].w;
  r0.z = 1 - r0.y;
  r0.x = r0.x * r0.z + r0.y;
  r0.y = 1 - r0.x;
  r0.z = cb0[14].x * cb0[14].x;
  r0.z = cb0[14].x * r0.z;
  o0.w = r0.z * r0.y + r0.x;
#if 1 // Luma: blend in sun shafts at full power, independently of the background luminance, we can in HDR!
#if ENABLE_FAKE_HDR // Avoid crazy looking shafts with fake HDR (we don't check "LumaSettings.DisplayMode" here, whatever)
  r0.xyz = t1.Sample(s0_s, w0.xy).xyz;
  r0.x = GetLuminance(r0.xyz);
  r0.x = saturate(r0.x);
  r0.x = exp2(r0.x * -3);
  r0.x = max(r0.x, 0.333);
#else
  r0.x = 1.0;
#endif
#elif 1 // Luma: vanilla emulation (there's steps in sun shafts on edges of bright and dark geometry otherwise)
  r0.xyz = t1.Sample(s0_s, w0.xy).xyz;
  r0.x = GetLuminance(r0.xyz); // Luma: fixed from BT.601 coeffs
  r0.x = saturate(r0.x); // Luma: add saturate to avoid going crazy or creating steps
  r0.x = exp2(r0.x * -3);
#endif
  r0.x = saturate(cb0[20].x * r0.x);
  o0.xyz = r1.xyz * r0.x;

#if ENABLE_FAKE_HDR // Apply the inverse of our following HDR boost to avoid sun shafts going crazy with it
  if (LumaSettings.DisplayMode == 1)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRReduction = 0.1;
    float saturationBoost = 0.2;
    float fakeHDRIntensity = -fakeHDRReduction;
    o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, saturationBoost);
  }
#endif

#if 1 // Luma: make them completely additive, without darkening the background (it made sense in SDR)
  o0.w = 1;
#endif
}