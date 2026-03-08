#include "Includes/Common.hlsl"

cbuffer PerFrameConstants : register(b3)
{
  row_major float4x4 ViewProjInv : packoffset(c0); // Not jittered
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

  // Add jitters already in the VS matrix because it's the most accurate way of calculating camera MVs 
  // given some operations are concatenated after the jitters should have applied
  float2 jitters = LumaData.GameData.CurrJitters;
  outUVJittered.xy = outUVNonJittered + jitters.xy;
  jitters *= float2(2.0, -2.0); // UV to NDC space
  float2 v0Jittered = v0;
  v0Jittered.xy += jitters;

  // Note: this projection matrix does not include jitters (which isn't good, if be better if it did and then they got removed, as we changed it to!).
  // Note that however the jitter matrix is very noisy!! Probably from low precision calculations. Same in the PS.
  r0.xyzw = v0Jittered.y * ViewProjInv._m10_m11_m12_m13;
  r0.xyzw += v0Jittered.x * ViewProjInv._m00_m01_m02_m03;
  r0.xyzw += ViewProjInv._m30_m31_m32_m33;
  r0.xyz = r0.xyz / r0.w;
  o2.xyz = InvFar.x * r0.xyz;
}