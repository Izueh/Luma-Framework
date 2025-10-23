// ---- Created with 3Dmigoto v1.3.16 on Sat Oct 18 22:53:34 2025

cbuffer _Globals : register(b0)
{
  float4 GlobalParams : packoffset(c0);
  float4 DofParamsA : packoffset(c1);
  float4 DofParamsB : packoffset(c2);
  float4 BloomColour : packoffset(c3);
  float4 VignetteInnerRgbPlusMul : packoffset(c4);
  float4 VignetteOuterRgbPlusAdd : packoffset(c5);
  float4 Tint2dColour : packoffset(c6);
  float4 BlurMatrixZZZ : packoffset(c7);
  float4 MotionBlurStencilValues : packoffset(c8);
  float4 AdaptiveLuminanceValues : packoffset(c9);
}

SamplerState SamplerSource_s : register(s0);
SamplerState SamplerBloom_s : register(s1);
SamplerState SamplerDof_s : register(s2);
SamplerState Sampler3dTint_s : register(s3);
SamplerState SamplerDepth_s : register(s4);
SamplerState SamplerSSAO_s : register(s6);
SamplerState SamplerParticles_s : register(s7);
SamplerState samplerLastAvgLuminance_s : register(s9);
Texture2D<float4> SamplerSourceTexture : register(t0);
Texture2D<float4> SamplerBloomTexture : register(t1);
Texture2D<float4> SamplerDofTexture : register(t2);
Texture3D<float4> Sampler3dTintTexture : register(t3);
Texture2D<float4> SamplerDepthTexture : register(t4);
Texture2D<float4> SamplerSSAOTexture : register(t6);
Texture2D<float4> SamplerParticlesTexture : register(t7);
Texture2D<float4> samplerLastAvgLuminanceTexture : register(t9);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = SamplerDepthTexture.Sample(SamplerDepth_s, v1.xy).x;
  r0.y = DofParamsA.y + -r0.x;
  r0.x = -DofParamsA.z + r0.x;
  r0.xy = DofParamsB.zy * r0.xy;
  r0.x = saturate(max(r0.y, r0.x));
  r0.x = DofParamsB.x * r0.x;
  r0.y = SamplerParticlesTexture.Sample(SamplerParticles_s, v1.xy).w;
  r0.z = SamplerSSAOTexture.Sample(SamplerSSAO_s, v1.xy).x;
  r1.xyz = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).xyz;
  r2.xyz = r1.xyz * r0.zzz;
  r1.xyz = -r1.xyz * r0.zzz + r1.xyz;
  r0.yzw = r0.yyy * r1.xyz + r2.xyz;
  r1.xyz = SamplerDofTexture.Sample(SamplerDof_s, v1.xy).xyz;
  r1.xyz = r1.xyz + -r0.yzw;
  r0.xyz = r0.xxx * r1.xyz + r0.yzw;
  r1.xy = samplerLastAvgLuminanceTexture.Sample(samplerLastAvgLuminance_s, float2(0.5,0.5)).xy;
  r0.w = r1.x + -r1.y;
  r1.x = cmp(abs(r0.w) < AdaptiveLuminanceValues.w);
  r0.w = r1.x ? 0 : r0.w;
  r1.x = AdaptiveLuminanceValues.z + -AdaptiveLuminanceValues.y;
  r0.w = r0.w / r1.x;
  r0.w = max(-1, r0.w);
  r0.w = min(1, r0.w);
  r0.w = AdaptiveLuminanceValues.x * r0.w;
  r1.x = r0.w * r0.w;
  r0.w = r1.x * r0.w;
  r0.xyz = r0.xyz * GlobalParams.xxx + r0.www;
  r1.xyz = SamplerBloomTexture.Sample(SamplerBloom_s, v1.xy).xyz;
  r2.xyz = BloomColour.xyz * r1.xyz;
  r2.xyz = saturate(r2.xyz * r0.xyz);
  r1.xyz = r1.xyz * BloomColour.xyz + -r2.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = Sampler3dTintTexture.Sample(Sampler3dTint_s, r0.xyz).xyz;
  r0.w = dot(v1.zw, v1.zw);
  r0.w = sqrt(r0.w);
  r0.w = saturate(VignetteOuterRgbPlusAdd.w + r0.w);
  r1.x = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r1.x * r0.w;
  r1.xyz = VignetteOuterRgbPlusAdd.xyz + -VignetteInnerRgbPlusMul.xyz;
  r1.xyz = r0.www * r1.xyz + VignetteInnerRgbPlusMul.xyz;
  o0.xyz = r0.xyz * r1.xyz + Tint2dColour.xyz;
  o0.w = 1;
  return;
}