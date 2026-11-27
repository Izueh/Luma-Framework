// Trails in the Sky 1st Chapter: Godray composite (0x1343A4D6).
// Vanilla: Screen blend in gamma space: c += (godray * godrayColor_g) * max(1.0 - c, 0.0).
// Luma: Matches vanilla Screen blend below 1.0 in linear space and smoothly transitions to linear additive HDR godrays above 1.0.

#include "Game/Common.hlsl"

cbuffer cb_godray : register(b2)
{
   float2 blurCenter_g : packoffset(c0);
   float2 blurScale_g : packoffset(c0.z);
   float zThreshold_g : packoffset(c1);
   float isFlip_g : packoffset(c1.y);
   float brightnessThreshold_g : packoffset(c1.z);
   float centricSharpness_g : packoffset(c1.w);
   float3 godrayColor_g : packoffset(c2);
   float4 uvClamp_g : packoffset(c3);
}

SamplerState samPoint : register(s0);
SamplerState samLinear : register(s1);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> godrayTexture : register(t1);

static const float GameGamma = 2.2;

void main(
   float4 v0 : SV_Position0,
   float4 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float3 godray = max(godrayTexture.SampleLevel(samLinear, v1.xy, 0).xyz * godrayColor_g, 0.0);
   float4 baseSample = colorTexture.SampleLevel(samPoint, v1.xy, 0);
   float3 base = max(baseSample.xyz, 0.0);

   o0.w = baseSample.w;

   if (LumaSettings.DisplayMode != 1)
   {
      o0.xyz = base + godray * max(1.0 - base, 0.0);
      return;
   }

   // Exact vanilla Screen-blend delta in linear space for [0, 1] range
   float3 sdrBase = saturate(base);
   float3 sdrResult = saturate(sdrBase + saturate(godray) * max(1.0 - sdrBase, 0.0));
   float3 sdrBaseLinear = pow(sdrBase, GameGamma);
   float3 sdrDeltaLinear = max(pow(sdrResult, GameGamma) - sdrBaseLinear, 0.0);

   // Smooth linear additive HDR contribution for highlights above 1.0
   float3 baseLinear = pow(base, GameGamma);
   float3 hdrGodrayLinear = pow(godray, GameGamma) * LumaSettings.GameSettings.hdr_godray_scale;
   float3 hdrLinear = baseLinear + max(sdrDeltaLinear, hdrGodrayLinear);

   o0.xyz = pow(max(hdrLinear, 0.0), 1.0 / GameGamma);
}
