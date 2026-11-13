#include "Game/Common.hlsl"

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[140];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : ATTRIBUTE0,
  float2 v1 : ATTRIBUTE1,
  out float4 o0 : TEXCOORD0,
  out float4 o1 : TEXCOORD1,
  out float4 o2 : TEXCOORD2,
  out float4 o3 : SV_POSITION0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;
  float4 renderResolution;
  if (LumaData.GameData.DrewUpscaling){
    renderResolution = LumaData.GameData.OutputResolution;
  } else {
    renderResolution.xy = cb2[1].xy;
    renderResolution.zw = cb2[2].xy;
  }
  r0.xy = v1.xy * renderResolution.xy + cb2[1].zw;
  o0.xy = cb2[2].zw * r0.xy;
  o1.z = cb1[24].x * renderResolution.x;
  r0.xy = v0.xy * renderResolution.xy + cb2[0].zw;
  r0.xy = renderResolution.zw * r0.xy;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  o1.x = renderResolution.x * r0.x;
  o1.y = renderResolution.y * -r0.y;
  r0.xy = float2(1,-1) * r0.xy;
  r0.zw = v0.zw;
  o2.xyzw = r0.xyzw;
  o3.xyzw = r0.xyzw;
  return;
}