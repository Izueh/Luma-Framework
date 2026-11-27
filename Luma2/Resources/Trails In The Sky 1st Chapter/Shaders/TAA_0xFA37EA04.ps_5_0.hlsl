// Trails in the Sky 1st Chapter: TAA resolve (the game's "cb_taa" pass, 0xFA37EA04).
// Luma: the three saturates on the current frame's color taps are removed. They clipped the scene to SDR (the highlights above 1 are
// kept, and the neighborhood clamp, the sharpening and the history blend work on them). The history texture needs to be float too.
// ---- Created with 3Dmigoto v1.4.1 on Fri Nov 20 14:03:00 2026

cbuffer cb_taa : register(b2)
{
  float2 jitter_g : packoffset(c0);
  float2 texelSize_g : packoffset(c0.z);
  float staticBlending_g : packoffset(c1);
  float dynamicBlending_g : packoffset(c1.y);
  float motionSensitivity_g : packoffset(c1.z);
  float sharpness_g : packoffset(c1.w);
  float2 prevResolutionScale_g : packoffset(c2);
}

SamplerState samPoint_s : register(s0);
SamplerState samLinear_s : register(s1);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> depthTexture : register(t1);
Texture2D<float4> historyTexture : register(t2);
Texture2D<float4> motionTexture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(-1,-1);
  r1.xy = float2(0,0);
  r1.z = depthTexture.SampleLevel(samPoint_s, v1.xy, 0).x;
  r2.xyzw = v1.xyxy - float4(texelSize_g, jitter_g); // The decompiler dropped the jitter (cb2[0].zwxy)
  r2.xy = saturate(r2.xy);
  r0.z = depthTexture.SampleLevel(samPoint_s, r2.xy, 0).x;
  r0.w = cmp(r0.z >= r1.z);
  r0.w = r0.w ? 1.000000 : 0;
  r0.xyz = -r1.yyz + r0.xyz;
  r0.xyz = r0.www * r0.xyz + r1.xyz;
  r1.xy = float2(1,-1);
  r3.xyzw = saturate(texelSize_g.xyxy * float4(1,-1,-1,1) + v1.xyxy);
  r1.z = depthTexture.SampleLevel(samPoint_s, r3.xy, 0).x;
  r3.z = depthTexture.SampleLevel(samPoint_s, r3.zw, 0).x;
  r0.w = cmp(r1.z >= r0.z);
  r0.w = r0.w ? 1.000000 : 0;
  r1.xyz = r1.xyz + -r0.yyz;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r3.xy = float2(-1,1);
  r0.w = cmp(r3.z >= r0.z);
  r0.w = r0.w ? 1.000000 : 0;
  r1.xyz = r3.xyz + -r0.xyz;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r1.xy = saturate(texelSize_g.xy + v1.xy);
  r0.w = depthTexture.SampleLevel(samPoint_s, r1.xy, 0).x;
  r0.z = cmp(r0.w >= r0.z);
  r0.z = r0.z ? 1.000000 : 0;
  r1.xy = float2(1,1) + -r0.xy;
  r0.xy = r0.zz * r1.xy + r0.xy;
  r0.xy = r0.xy * texelSize_g.xy + v1.xy;
  r0.xy = motionTexture.SampleLevel(samLinear_s, r0.xy, 0).xy;
  r0.xy = texelSize_g.xy * r0.xy;
  r0.z = dot(r0.xy, r0.xy);
  r0.xy = v1.xy * prevResolutionScale_g.xy + r0.xy;
  r0.xyw = historyTexture.SampleLevel(samLinear_s, r0.xy, 0).xyz;
  r0.z = sqrt(r0.z);
  r1.x = 100 * r0.z;
  r0.z = saturate(motionSensitivity_g * r0.z);
  r1.x = min(1, r1.x);
  r1.x = r1.x * -3.75 + 4;
  r1.yz = saturate(-texelSize_g.xy * float2(0.5,0.5) + r2.zw);
  r1.yzw = colorTexture.SampleLevel(samLinear_s, r1.yz, 0).xyz;
  r2.xy = saturate(texelSize_g.xy * float2(0.5,0.5) + r2.zw);
  r3.xyz = colorTexture.SampleLevel(samLinear_s, r2.zw, 0).xyz;
  r2.xyz = colorTexture.SampleLevel(samLinear_s, r2.xy, 0).xyz;
  r4.xyz = r2.xyz + r1.yzw;
  r5.xyz = r3.xyz + r3.xyz;
  r4.xyz = r4.xyz * float3(4,4,4) + -r5.xyz;
  r5.xyz = -r4.xyz * float3(0.166666999,0.166666999,0.166666999) + r3.xyz;
  r5.xyz = sharpness_g * r5.xyz;
  r3.xyz = r5.xyz * float3(2.71828198,2.71828198,2.71828198) + r3.xyz;
  r3.xyz = max(float3(0,0,0), r3.xyz);
  r3.xyz = min(float3(65472,65472,65472), r3.xyz);
  r4.xyz = r4.xyz + r3.xyz;
  r4.xyz = float3(0.142857,0.142857,0.142857) * r4.xyz;
  r2.w = dot(r4.xyz, float3(0.298999995,0.587000012,0.114));
  r3.w = dot(r3.xyz, float3(0.298999995,0.587000012,0.114));
  r2.w = -r3.w + r2.w;
  r4.xyz = min(r2.xyz, r1.yzw);
  r1.yzw = max(r2.xyz, r1.yzw);
  r1.yzw = r1.xxx * abs(r2.www) + r1.yzw;
  r2.xyz = -r1.xxx * abs(r2.www) + r4.xyz;
  r4.xyz = -r2.xyz + r1.yzw;
  r1.xyz = r2.xyz + r1.yzw;
  r2.xyz = float3(0.5,0.5,0.5) * r4.xyz;
  r0.xyw = -r1.xyz * float3(0.5,0.5,0.5) + r0.xyw;
  r1.xyz = float3(0.5,0.5,0.5) * r1.xyz;
  r4.xyz = float3(9.99999975e-05,9.99999975e-05,9.99999975e-05) + r0.xyw;
  r2.xyz = r2.xyz / r4.xyz;
  r1.w = min(abs(r2.y), abs(r2.z));
  r1.w = min(abs(r2.x), r1.w);
  r1.w = min(1, r1.w);
  r0.xyw = r0.xyw * r1.www + r1.xyz;
  r0.xyw = r0.xyw + -r3.xyz;
  r1.x = dynamicBlending_g + -staticBlending_g;
  r0.z = r0.z * r1.x + staticBlending_g;
  r0.xyz = r0.zzz * r0.xyw + r3.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  o0.xyz = min(float3(65472,65472,65472), r0.xyz);
  o0.w = 1;
  return;
}
