#include "../Includes/Common.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

cbuffer _Globals : register(b0)
{
  float4 Scat[4] : packoffset(c0);
  float4 litDir : packoffset(c4);
  float4 litCol : packoffset(c5);
  float4 skyPos : packoffset(c6);
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = dot(v1.xyz, v1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v1.xyz * r0.xxx;
  r0.w = r0.y * r0.y;
  r0.w = r0.w * skyPos.y + skyPos.z;
  r0.w = sqrt(r0.w);
  r0.w = -r0.y * skyPos.x + -r0.w;
  r0.x = dot(r0.xyz, -litDir.xyz);
  r0.y = abs(r0.w) * v2.x + v2.y;
  r0.yzw = Scat[0].xyz * r0.yyy;
  r0.yzw = exp2(r0.yzw);
  r0.yzw = min(float3(1,1,1), r0.yzw);
  r0.yzw = float3(1,1,1) + -r0.yzw;
  r1.x = Scat[2].w * r0.x + Scat[3].w;
  r0.x = r0.x * r0.x + 1;
  r1.yzw = Scat[2].xyz * r0.xxx;
  r0.x = max(9.99999975e-005, r1.x);
  r0.x = log2(r0.x);
  r0.x = -1.5 * r0.x;
  r0.x = exp2(r0.x);
  r1.xyz = Scat[3].xyz * r0.xxx + r1.yzw;
  r1.xyz = Scat[1].xyz + r1.xyz;
  o0.xyz = r1.xyz * r0.yzw;
  o0.w = 1;
  
  bool doHDR = !ShouldForceSDR(v0.xy * LumaSettings.SwapchainInvSize ) && LumaSettings.DisplayMode == 1;
  if (doHDR)
  {
#if ENABLE_HDR_BOOST // TODO: also add this to the sun shaders (0x9F263F7F and 0x217C7478 (any other?)), though they are extremely complicated
    float normalizationPoint = 0.025;
    float fakeHDRIntensity = 0.25;
    float fakeHDRSaturation = 0.2;
    r0.rgb = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(r0.rgb), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif
  }
}