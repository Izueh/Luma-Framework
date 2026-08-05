// ---- Created with 3Dmigoto v1.4.1 on Sun Apr 19 01:50:21 2026
#include "Includes/Common.hlsl"
cbuffer _Globals : register(b0)
{
  float4 screenDims : packoffset(c0);
  float4 blurCoCOffsets[5] : packoffset(c1);
  float4 maxCoCOffsets[16] : packoffset(c6);
}

cbuffer cbDof : register(b1)
{
  float4 unprojectParam : packoffset(c0);
  float4 CoCParam0 : packoffset(c1);
  float4 CoCParam1 : packoffset(c2);
  float4 CoCParam2 : packoffset(c3);
  float4 CoCParam3 : packoffset(c4);
  float4 fullscreenDims : packoffset(c5);
  float4 vignetteBlurParam0 : packoffset(c6);
  float4 vignetteBlurParam1 : packoffset(c7);
  float4 uvJitterOffset : packoffset(c8);
  float4x4 motionMatrix : packoffset(c9);
  float blendBokeh_param0 : packoffset(c13);
  float blendBokeh_param1 : packoffset(c13.y);
  float gamePaused : packoffset(c13.z);
  float spare : packoffset(c14);
}

SamplerState pointSampler_s : register(s0);
Texture2D<float4> srcColorTex : register(t0);
Texture2D<float4> srcCoCTex : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = srcColorTex.SampleLevel(pointSampler_s, v1.xy, 0).xyzw;
  r1.xy = srcCoCTex.SampleLevel(pointSampler_s, v1.xy, 0).xy;
  r1.x = max(r1.x, r1.y);
  r1.y = cmp(CoCParam3.w < r1.x);
  if (r1.y != 0) {
    float2 scale = 1.0f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
      scale = LumaData.RenderResolutionScale;
    }
    r2.xyzw = screenDims.zwzw * scale.xyxy * float4(-0.5,-0.5,0.5,-0.5) + v1.xyxy;
    r1.yz = screenDims.zw * scale.xy * float2(-0.5,0.5) + v1.xy;
    r3.xy = screenDims.zw * scale.xy + v1.xy;
    r4.xyzw = srcColorTex.Gather(pointSampler_s, r2.xy).xyzw;
    r5.xyzw = srcColorTex.Gather(pointSampler_s, r2.xy).xyzw;
    r6.xyzw = srcColorTex.Gather(pointSampler_s, r2.xy).xyzw;
    r7.xyzw = srcCoCTex.Gather(pointSampler_s, r2.xy).xyzw;
    r8.xyzw = srcCoCTex.Gather(pointSampler_s, r2.xy).xyzw;
    r7.xyzw = max(r8.xyzw, r7.xyzw);
    r7.xyzw = saturate(r7.xyzw * CoCParam0.wwww + float4(-1,-1,-1,-1));
    r8.x = r4.x;
    r8.y = r5.x;
    r8.z = r6.x;
    r8.xyz = r8.xyz + -r0.xyz;
    r8.xyz = r1.xxx * r8.xyz + r0.xyz;
    r8.xyz = r8.xyz * r7.xxx;
    r9.xyz = r0.xyz;
    r9.w = 1;
    r8.w = r7.x;
    r8.xyzw = r9.xyzw + r8.xyzw;
    r9.x = r4.y;
    r9.y = r5.y;
    r9.z = r6.y;
    r9.xyz = r9.xyz + -r0.xyz;
    r9.xyz = r1.xxx * r9.xyz + r0.xyz;
    r9.xyz = r9.xyz * r7.yyy;
    r9.w = r7.y;
    r8.xyzw = r9.xyzw + r8.xyzw;
    r6.x = r4.z;
    r6.y = r5.z;
    r4.xyz = r6.xyz + -r0.xyz;
    r4.xyz = r1.xxx * r4.xyz + r0.xyz;
    r9.xyz = r4.xyz * r7.zzz;
    r9.w = r7.z;
    r8.xyzw = r9.xyzw + r8.xyzw;
    r6.x = r4.w;
    r6.y = r5.w;
    r4.xyz = r6.xyw + -r0.xyz;
    r4.xyz = r1.xxx * r4.xyz + r0.xyz;
    r7.xyz = r4.xyz * r7.www;
    r4.xyzw = r8.xyzw + r7.xyzw;
    r2.xy = srcColorTex.Gather(pointSampler_s, r2.zw).yz;
    r3.zw = srcColorTex.Gather(pointSampler_s, r2.zw).yz;
    r5.yz = srcColorTex.Gather(pointSampler_s, r2.zw).yz;
    r6.xy = srcCoCTex.Gather(pointSampler_s, r2.zw).yz;
    r2.zw = srcCoCTex.Gather(pointSampler_s, r2.zw).yz;
    r2.zw = max(r6.xy, r2.zw);
    r6.xw = saturate(r2.zw * CoCParam0.ww + float2(-1,-1));
    r7.x = r2.x;
    r7.y = r3.z;
    r7.z = r5.y;
    r2.xzw = r7.xyz + -r0.xyz;
    r2.xzw = r1.xxx * r2.xzw + r0.xyz;
    r7.xyz = r2.xzw * r6.xxx;
    r7.w = r6.x;
    r4.xyzw = r7.xyzw + r4.xyzw;
    r5.x = r2.y;
    r5.y = r3.w;
    r2.xyz = r5.xyz + -r0.xyz;
    r2.xyz = r1.xxx * r2.xyz + r0.xyz;
    r6.xyz = r2.xyz * r6.www;
    r2.xyzw = r6.xyzw + r4.xyzw;
    r3.zw = srcColorTex.Gather(pointSampler_s, r1.yz).xy;
    r4.xy = srcColorTex.Gather(pointSampler_s, r1.yz).xy;
    r5.xz = srcColorTex.Gather(pointSampler_s, r1.yz).xy;
    r4.zw = srcCoCTex.Gather(pointSampler_s, r1.yz).xy;
    r1.yz = srcCoCTex.Gather(pointSampler_s, r1.yz).xy;
    r1.yz = max(r4.zw, r1.yz);
    r6.xw = saturate(r1.yz * CoCParam0.ww + float2(-1,-1));
    r7.x = r3.z;
    r7.y = r4.x;
    r7.z = r5.x;
    r1.yzw = r7.xyz + -r0.xyz;
    r1.yzw = r1.xxx * r1.yzw + r0.xyz;
    r7.xyz = r1.yzw * r6.xxx;
    r7.w = r6.x;
    r2.xyzw = r7.xyzw + r2.xyzw;
    r5.x = r3.w;
    r5.y = r4.y;
    r1.yzw = r5.xyz + -r0.xyz;
    r1.yzw = r1.xxx * r1.yzw + r0.xyz;
    r6.xyz = r1.yzw * r6.www;
    r2.xyzw = r6.xyzw + r2.xyzw;
    r1.yzw = srcColorTex.SampleLevel(pointSampler_s, r3.xy, 0).xyz;
    r3.xy = srcCoCTex.SampleLevel(pointSampler_s, r3.xy, 0).xy;
    r3.x = max(r3.x, r3.y);
    r3.w = saturate(r3.x * CoCParam0.w + -1);
    r1.yzw = r1.yzw + -r0.xyz;
    r1.xyz = r1.xxx * r1.yzw + r0.xyz;
    r3.xyz = r1.xyz * r3.www;
    r1.xyzw = r3.wxyz + r2.wxyz;
    r0.xyz = r1.yzw;
  } else {
    r1.x = 1;
  }
  o0.xyz = r0.xyz / r1.xxx;
  o0.w = r0.w;
  return;
}