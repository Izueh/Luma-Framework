#include "../Includes/Common.hlsl"

#ifndef IMPROVED_BLOOM
#define IMPROVED_BLOOM 1
#endif

cbuffer _Globals : register(b0)
{
  float4 g_vPosOffset : packoffset(c0) = {0,0,0,0};
  float4 g_vUVOffsetSampling[16] : packoffset(c1);
  float4 g_vTexParam : packoffset(c17) = {1,1,0,0};
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0,
  out float4 o2 : TEXCOORD1,
  out float4 o3 : TEXCOORD2,
  out float4 o4 : TEXCOORD3)
{
  float4 r0;
  r0.xy = g_vPosOffset.xy + v0.xy;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  o0.xy = float2(1,-1) * r0.xy;
  o0.zw = v0.zw;
#if 0 // Luma: attempted fix for bloom being stretched in ultrawide, though this turned out to be a wrong fix, vanilla is already scaled perfectly by the game
  float targetAspectRatio = 16.0 / 9.0;
  float currentAspectRatio = LumaSettings.SwapchainSize.x * LumaSettings.SwapchainInvSize.y;
  float2 aspectRatioScale = float2(targetAspectRatio / currentAspectRatio, 1.0);
#else
  float2 aspectRatioScale = 1.0;
#endif
#if IMPROVED_BLOOM
  aspectRatioScale.x *= 0.5;
#endif
  r0.xy = g_vUVOffsetSampling[3].xy * aspectRatioScale.xy + v1.xy;
  o1.xy = r0.xy * g_vTexParam.xy + g_vTexParam.zw;
  r0.xyzw = g_vUVOffsetSampling[0].xyzw * aspectRatioScale.xyxy + v1.xyxy;
  o2.xyzw = r0.xyzw * g_vTexParam.xyxy + g_vTexParam.zwzw;
  r0.xyzw = g_vUVOffsetSampling[1].xyzw * aspectRatioScale.xyxy + v1.xyxy;
  o3.xyzw = r0.xyzw * g_vTexParam.xyxy + g_vTexParam.zwzw;
  r0.xyzw = g_vUVOffsetSampling[2].xyzw * aspectRatioScale.xyxy + v1.xyxy;
  o4.xyzw = r0.xyzw * g_vTexParam.xyxy + g_vTexParam.zwzw;
}