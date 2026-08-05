// ---- Created with 3Dmigoto v1.4.1 on Sun Apr 19 01:50:24 2026
#include "Includes/Common.hlsl"
cbuffer _Globals : register(b0)
{
  float4 screenDims : packoffset(c0);
  float4 blurCoCOffsets[5] : packoffset(c1);
  float4 maxCoCOffsets[16] : packoffset(c6);
}

SamplerState pointSampler_s : register(s0);
Texture2D<float4> srcCoCTex : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  float2 scale = 1.0f;
  if (LumaData.GameData.IsUpscaling != 0)
  {
    scale = LumaData.RenderResolutionScale;
  }
  r0.xy = screenDims.zw * scale * float2(-0.5,-0.5) + v1.xy;
  r1.xyzw = srcCoCTex.Gather(pointSampler_s, r0.xy).xyzw;
  r0.xyzw = srcCoCTex.Gather(pointSampler_s, r0.xy).xyzw;
  r1.x = max(r1.z, r1.x);
  r1.x = max(r1.x, r1.y);
  o0.x = max(r1.w, r1.x);
  r0.x = min(r0.z, r0.x);
  r0.x = min(r0.x, r0.y);
  o0.y = min(r0.w, r0.x);
  o0.zw = float2(0,0);
  return;
}