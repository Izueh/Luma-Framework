#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 vLightShaftLightPos : packoffset(c0);
  float4 vLightShaftPower : packoffset(c1);
  float4 vScreenSize : packoffset(c2) = {1920,1080,1920,1080};
  float4 vLightShaftBlurWeights[4] : packoffset(c3);
  float fBloomWeight : packoffset(c7) = {0.5};
  float4 vAnamorphicBloomColorScale : packoffset(c8) = {1,1,1,1};
  float fAnamorphicBloomWeight : packoffset(c9) = {1};
}

SamplerState smplLightShaftLinWork1_s : register(s0);
SamplerState smplBloom_s : register(s1);
SamplerState smplAnamorphicBloom_s : register(s2);
Texture2D<float4> smplLightShaftLinWork1_Tex : register(t0);
Texture2D<float4> smplBloom_Tex : register(t1);
Texture2D<float4> smplAnamorphicBloom_Tex : register(t2);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  int4 r0i;
  r0.xy = vLightShaftLightPos.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r0.z = 4 * vLightShaftPower.w;
  r0.z = 0.5 * (r0.z / vScreenSize.w);
  r0.xy = -v1.xy + r0.xy;
#if 0 // Luma: attempted fix sun shafts horizontal scale (I thought they were stretched in UW but they weren't)
  float aspectRatio = vScreenSize.x / vScreenSize.y;
  float aspectRatioRatio = (16.f / 9.f) / aspectRatio;
#else
  float aspectRatioRatio = 1.0;
#endif
  r0.xy = r0.xy * float2(r0.z * aspectRatioRatio, r0.z);
  r1.xyzw = float4(0,0,0,0);
  r0i.z = 0;
  while (true) {
    if (r0i.z >= 4) break;
    r0.w = r0i.z << 2; // ishl+itof
    r2.xy = r0.xy * r0.ww + v1.xy;
    r2.xyzw = smplLightShaftLinWork1_Tex.Sample(smplLightShaftLinWork1_s, r2.xy).xyzw;
    r2.xyzw = vLightShaftBlurWeights[r0i.z].xxxx * r2.xyzw + r1.xyzw;
    r3.xyz = float3(1,2,3) + r0.www;
    r4.xyzw = r0.xyxy * r3.xxyy + v1.xyxy;
    r5.xyzw = smplLightShaftLinWork1_Tex.Sample(smplLightShaftLinWork1_s, r4.xy).xyzw;
    r2.xyzw = vLightShaftBlurWeights[r0i.z].yyyy * r5.xyzw + r2.xyzw;
    r4.xyzw = smplLightShaftLinWork1_Tex.Sample(smplLightShaftLinWork1_s, r4.zw).xyzw;
    r2.xyzw = vLightShaftBlurWeights[r0i.z].zzzz * r4.xyzw + r2.xyzw;
    r3.xy = r0.xy * r3.zz + v1.xy;
    r3.xyzw = smplLightShaftLinWork1_Tex.Sample(smplLightShaftLinWork1_s, r3.xy).xyzw;
    r1.xyzw = vLightShaftBlurWeights[r0i.z].wwww * r3.xyzw + r2.xyzw;
    r0i.z = r0i.z + 1;
  }
  r0.xyz = smplBloom_Tex.Sample(smplBloom_s, v1.xy).xyz;
  r0.xyz = fBloomWeight * r0.xyz;
  r0.xyz = r1.xyz * vLightShaftPower.xyz + r0.xyz;
  r1.xyz = smplAnamorphicBloom_Tex.Sample(smplAnamorphicBloom_s, v1.xy).xyz;
  r1.xyz = vAnamorphicBloomColorScale.xyz * r1.xyz;
  o0.xyz = r1.xyz * fAnamorphicBloomWeight + r0.xyz;
  o0.w = r1.w;
}