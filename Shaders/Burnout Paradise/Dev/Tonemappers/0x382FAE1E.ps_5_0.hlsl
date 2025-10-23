// ---- Created with 3Dmigoto v1.3.16 on Sat Oct 18 22:53:30 2025

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
SamplerState SamplerDepth_s : register(s4);
SamplerState samplerLastAvgLuminance_s : register(s9);
Texture2D<float4> SamplerSourceTexture : register(t0);
Texture2D<float4> SamplerBloomTexture : register(t1);
Texture2D<float4> SamplerDofTexture : register(t2);
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
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = SamplerBloomTexture.Sample(SamplerBloom_s, v1.xy).xyz;
  r1.xyz = SamplerDofTexture.Sample(SamplerDof_s, v1.xy).xyz;
  r0.w = SamplerDepthTexture.Sample(SamplerDepth_s, v1.xy).x;
  r1.w = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy).w;
  r1.w = cmp(r1.w < 0.699999988);
  r1.w = r1.w ? MotionBlurStencilValues.y : MotionBlurStencilValues.x;
  r2.xyz = BlurMatrixZZZ.xyz * r0.www + v2.xyz;
  r2.xy = v1.xy * r2.zz + r2.xy;
  r2.xy = r2.xy * r1.ww;
  r1.w = v0.y * v0.y;
  r1.w = 0.381966025 * r1.w;
  r1.w = v0.x * 0.618034005 + r1.w;
  r1.w = frac(r1.w);
  r2.zw = float2(0.0625,0.0625) * r2.xy;
  r2.zw = r2.zw * r1.ww + v1.xy;
  r3.xyz = SamplerSourceTexture.Sample(SamplerSource_s, r2.zw).xyz;
  r4.xyz = r3.xyz;
  r5.xy = r2.zw;
  r1.w = 1;
  while (true) {
    r3.w = cmp((int)r1.w >= 16);
    if (r3.w != 0) break;
    r5.xy = r2.xy * float2(0.0625,0.0625) + r5.xy;
    r6.xyz = SamplerSourceTexture.Sample(SamplerSource_s, r5.xy).xyz;
    r4.xyz = r6.xyz + r4.xyz;
    r1.w = (int)r1.w + 1;
  }
  r2.xyz = float3(0.0625,0.0625,0.0625) * r4.xyz;
  r1.w = DofParamsA.y + -r0.w;
  r1.w = DofParamsB.y * r1.w;
  r0.w = -DofParamsA.z + r0.w;
  r0.w = DofParamsB.z * r0.w;
  r0.w = saturate(max(r1.w, r0.w));
  r0.w = DofParamsB.x * r0.w;
  r1.xyz = -r4.xyz * float3(0.0625,0.0625,0.0625) + r1.xyz;
  r1.xyz = r0.www * r1.xyz + r2.xyz;
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
  r1.xyz = r1.xyz * GlobalParams.xxx + r0.www;
  r2.xyz = BloomColour.xyz * r0.xyz;
  r2.xyz = saturate(r2.xyz * r1.xyz);
  r0.xyz = r0.xyz * BloomColour.xyz + -r2.xyz;
  r0.xyz = r1.xyz + r0.xyz;
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