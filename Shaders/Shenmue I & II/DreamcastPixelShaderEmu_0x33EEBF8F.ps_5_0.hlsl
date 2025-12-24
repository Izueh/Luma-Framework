#include "../Includes/Common.hlsl"

cbuffer ShaderParams : register(b1)
{
  float3 gFogColor : packoffset(c0);
  uint gFogEnabled : packoffset(c0.w);
  float4 gTextureFactor : packoffset(c1);
  uint gAlphaTestEnabled : packoffset(c2);
  float gAlphaRef : packoffset(c2.y);
  uint gAlphaFunc : packoffset(c2.z);
  uint gAlphaOP : packoffset(c2.w);
  uint gAlphaARG0 : packoffset(c3);
  uint gAlphaARG1 : packoffset(c3.y);
  uint gAlphaARG2 : packoffset(c3.z);
  uint gSpecularEnable : packoffset(c3.w);
  uint gRenderMode : packoffset(c4);
  float gDepthBias : packoffset(c4.y);
  uint gUseEnvMap : packoffset(c4.z);
  uint gDisableReflex : packoffset(c4.w);
  float4 Constant0_0 : packoffset(c5);
  float4 Constant0_1 : packoffset(c6);
  float4 Constant0_2 : packoffset(c7);
  float4 Constant0_3 : packoffset(c8);
  float4 Constant0_4 : packoffset(c9);
  float4 Constant0_5 : packoffset(c10);
  float4 Constant0_6 : packoffset(c11);
  float4 Constant0_7 : packoffset(c12);
  float4 Constant1_0 : packoffset(c13);
  float4 Constant1_1 : packoffset(c14);
  float4 Constant1_2 : packoffset(c15);
  float4 Constant1_3 : packoffset(c16);
  float4 Constant1_4 : packoffset(c17);
  float4 Constant1_5 : packoffset(c18);
  float4 Constant1_6 : packoffset(c19);
  float4 Constant1_7 : packoffset(c20);
  float4 c[117] : packoffset(c21);
}

SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> diffuseTexture : register(t0);

#define cmp

// Transparency only
void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = diffuseTexture.Sample(DiffuseSampler_s, v2.xy).xyzw;
  r1.xyzw = v1.xyzw * r0.xyzw;
  if (gAlphaTestEnabled != 0) {
    switch (gAlphaFunc) {
      case 0 :      if (-1 != 0) discard;
      break;
      case 1 :      r0.x = cmp(r1.w < gAlphaRef);
      if (r0.x == 0) discard;
      break;
      case 2 :      r0.x = r0.w * v1.w + -gAlphaRef;
      r0.x = cmp(abs(r0.x) < 0.025);
      if (r0.x == 0) discard;
      break;
      case 3 :      r0.x = cmp(gAlphaRef >= r1.w);
      if (r0.x == 0) discard;
      break;
      case 4 :      r0.x = cmp(gAlphaRef < r1.w);
      if (r0.x == 0) discard;
      break;
      case 5 :      r0.x = r0.w * v1.w + -gAlphaRef;
      r0.x = cmp(0.00001 < abs(r0.x));
      if (r0.x == 0) discard;
      break;
      case 6 :      r0.x = cmp(r1.w >= gAlphaRef);
      if (r0.x == 0) discard;
      break;
      case 7 :      break;
      default :
      break;
    }
  }
  o0.xyzw = r1.xyzw;
  
#if ENABLE_HDR_BOOST
  o0.rgb = gamma_to_linear(o0.rgb);
  o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits) * 5;
  o0.rgb = linear_to_gamma(o0.rgb);
#endif
}