// Trails in the Sky 1st Chapter: Godray source extraction (0xD0B53A29).
// Vanilla: Computes color * max(luma - brightnessThreshold_g, 0.0) * radialFalloff into an R8G8B8A8_UNORM RT (which clamped to 1.0).
// Luma: With the 960x540 RT upgraded to R16G16B16A16_FLOAT, unclamped HDR values cause a quadratic blowup (~29x on the sun).
//       We cap the threshold weight and smoothly roll off the extracted source toward ~1.2 using CInfinityRollOff so godrays
//       match the SDR intensity with subtle HDR headroom instead of washing out the sky.

// clang-format off
#include "Game/Common.hlsl"
#include "Includes/Tonemap/CInfinityRolloff.hlsl"
// clang-format on

cbuffer cb_scene : register(b0)
{
   row_major float4x4 view_g : packoffset(c0);
   row_major float4x4 viewInv_g : packoffset(c4);
   row_major float4x4 proj_g : packoffset(c8);
   row_major float4x4 projInv_g : packoffset(c12);
}

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

SamplerState samLinear : register(s0);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> depthTexture : register(t1);

static const float GodraySourceAnchor = 0.85;

void main(
   float4 v0 : SV_Position0,
   float4 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float2 toCenter = blurCenter_g - v1.xy;
   float dist = min(length(toCenter), 1.0);
   float radial = pow(max(1.0 - dist, 0.0), centricSharpness_g);

   float centerDist = min(length(blurCenter_g - 0.5), 1.0);
   float falloff = radial * (1.0 - centerDist);

   float4 colorSample = colorTexture.SampleLevel(samLinear, v1.xy, 0);
   float3 color = max(colorSample.xyz, 0.0);
   float luma = dot(color, float3(0.299, 0.587, 0.114));

   // Prevent quadratic explosion from HDR values > 1 while preserving exact SDR behavior below 1
   float weight = saturate(luma - brightnessThreshold_g);
   float3 outColor = color * weight * falloff;

   if (LumaSettings.DisplayMode == 1)
   {
      const float peak = max(LumaSettings.GameSettings.godray_source_peak, GodraySourceAnchor + 0.01);
      outColor = Tonemap::CInfinityRollOff::MaxChannel(outColor, peak, GodraySourceAnchor, 1.5);
   }
   else
   {
      outColor = saturate(outColor);
   }

   // Vanilla depth mask (only sky / distant pixels beyond zThreshold_g emit godrays)
   float depth = depthTexture.SampleLevel(samLinear, v1.xy, 0).x;
   float viewZ = dot(projInv_g[2].zw, float2(depth, 1.0)) / dot(projInv_g[3].zw, float2(depth, 1.0));
   float depthMask = (-viewZ < zThreshold_g) ? 0.0 : 1.0;
   outColor *= depthMask;

   o0.xyz = (isFlip_g < 0.0) ? float3(0.0, 0.0, 0.0) : outColor;
   o0.w = colorSample.w;
}
