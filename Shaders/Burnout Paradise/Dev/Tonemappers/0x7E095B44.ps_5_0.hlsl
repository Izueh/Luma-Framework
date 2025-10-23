// ---- Created with 3Dmigoto v1.3.16 on Sat Oct 18 22:53:32 2025

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
SamplerState Sampler3dTint_s : register(s3);
SamplerState SamplerDepth_s : register(s4);
SamplerState samplerLastAvgLuminance_s : register(s9);
Texture2D<float4> SamplerSourceTexture : register(t0);
Texture2D<float4> SamplerBloomTexture : register(t1);
Texture3D<float4> Sampler3dTintTexture : register(t3);
Texture2D<float4> SamplerDepthTexture : register(t4);
Texture2D<float4> samplerLastAvgLuminanceTexture : register(t9);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = SamplerBloomTexture.Sample(SamplerBloom_s, v1.xy).xyz;
  r0.w = SamplerDepthTexture.Sample(SamplerDepth_s, v1.xy).x;
  r1.x = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).w;
  r1.x = cmp(r1.x < 0.699999988);
  r1.x = r1.x ? MotionBlurStencilValues.y : MotionBlurStencilValues.x;
  r1.yzw = BlurMatrixZZZ.xyz * r0.www + v2.xyz;
  r1.yz = v1.xy * r1.ww + r1.yz;
  r1.xy = r1.yz * r1.xx;
  r0.w = v0.y * v0.y;
  r0.w = 0.381966025 * r0.w;
  r0.w = v0.x * 0.618034005 + r0.w;
  r0.w = frac(r0.w);
  r1.zw = float2(0.0625,0.0625) * r1.xy;
  r1.zw = r1.zw * r0.ww + v1.xy;
  r2.xyz = SamplerSourceTexture.Sample(SamplerSource_s, r1.zw).xyz;
  r3.xyz = r2.xyz;
  r4.xy = r1.zw;
  r0.w = 1;
  while (true) {
    r2.w = cmp((int)r0.w >= 16);
    if (r2.w != 0) break;
    r4.xy = r1.xy * float2(0.0625,0.0625) + r4.xy;
    r5.xyz = SamplerSourceTexture.Sample(SamplerSource_s, r4.xy).xyz;
    r3.xyz = r5.xyz + r3.xyz;
    r0.w = (int)r0.w + 1;
  }
  r1.xyz = GlobalParams.xxx * r3.xyz;
  r2.xy = samplerLastAvgLuminanceTexture.Sample(samplerLastAvgLuminance_s, float2(0.5,0.5)).xy;
  r0.w = r2.x + -r2.y;
  r1.w = cmp(abs(r0.w) < AdaptiveLuminanceValues.w);
  r0.w = r1.w ? 0 : r0.w;
  r1.w = AdaptiveLuminanceValues.z + -AdaptiveLuminanceValues.y;
  r0.w = r0.w / r1.w;
  r0.w = max(-1, r0.w);
  r0.w = min(1, r0.w);
  r0.w = AdaptiveLuminanceValues.x * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  r1.xyz = r1.xyz * float3(0.0625,0.0625,0.0625) + r0.www;
  r2.xyz = BloomColour.xyz * r0.xyz;
  r2.xyz = saturate(r2.xyz * r1.xyz);
  r0.xyz = r0.xyz * BloomColour.xyz + -r2.xyz;
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