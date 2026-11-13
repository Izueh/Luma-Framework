#include "Game/Common.hlsl"

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : ATTRIBUTE0,
  float2 v1 : ATTRIBUTE1,
  out float4 o0 : TEXCOORD0,
  out float4 o1 : SV_POSITION0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;
  float resolutionScale;
  float4 resolution;
  if(LumaData.GameData.DrewUpscaling != 0) {
    resolution = LumaData.GameData.OutputResolution;
    resolutionScale = LumaData.GameData.ResolutionScale.y;
  } else {
    resolution.xy = cb0[1].xy;
    resolution.zw = cb0[2].xy;
    resolutionScale = 1.0f;
  }
  r0.xy = v1.xy * (resolution.xy) + (cb0[1].zw * resolutionScale);
  o0.xy = cb0[2].zw * r0.xy;
  r0.xy = v0.xy * (resolution.xy) + (cb0[0].zw * resolutionScale);
  r0.xy = (resolution.zw) * r0.xy;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.xy = float2(1,-1) * r0.xy;
  o0.zw = r0.xy;
  o1.xy = r0.xy;
  o1.zw = v0.zw;
  return;
}