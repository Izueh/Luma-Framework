#include "Includes/Common.hlsl"

cbuffer PerFrameConstants : register(b3)
{
  row_major float4x4 ViewProjInv : packoffset(c0);
  float4 InvFar : packoffset(c4);
}

void main(
  float2 v0 : POSITION0,
  out float4 o0 : SV_Position0,
  out float2 outUVJittered : TEXCOORD0,
  out float2 outUVNonJittered : TEXCOORD1,
  out float3 o2 : TEXCOORD2)
{
  float4 r0;
  o0.xy = v0.xy;
  o0.zw = float2(0,1);
  outUVNonJittered.xy = v0.xy * float2(0.5,-0.5) + float2(0.5,0.5); // NDC to UV space

  // Add jittered already in the VS matrix because it's the most accurate way of calculating camera MVs 
  // given some operations are concatenated after the jitters should have applied
  float2 jitters = float2(asfloat(LumaData.CustomData1), asfloat(LumaData.CustomData2));
  outUVJittered.xy = outUVNonJittered + jitters.xy;
  jitters *= float2(2.0, -2.0); // NDC to UV space
  v0.xy += jitters;

  // Note: this projection matrix does not include jitters (which isn't good!) // TODO: or is it jittered??? Does this game even have jitters?
  r0.xyzw = v0.y * ViewProjInv._m10_m11_m12_m13;
  r0.xyzw += v0.x * ViewProjInv._m00_m01_m02_m03;
  r0.xyzw += ViewProjInv._m30_m31_m32_m33;
  r0.xyz = r0.xyz / r0.w;
  o2.xyz = InvFar.x * r0.xyz;
}