#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float2 fParam_DepthCastScaleOffset : packoffset(c0) = {1,0};
  float4 fParam_DepthOfFieldFactorScaleOffset : packoffset(c1) = {0.5,0.5,2,-1};
  float4 fParam_HDRFormatFactor_LOGRGB : packoffset(c2);
  float4 fParam_HDRFormatFactor_RGBALUM : packoffset(c3);
  float4 fParam_HDRFormatFactor_REINHARDRGB : packoffset(c4);
  float2 fParam_ScreenSpaceScale : packoffset(c5) = {1,-1};
  float4x4 m44_ModelViewProject : packoffset(c6);
  float4 fParam_GammaCorrection : packoffset(c10) = {0.454545468,0.454545468,0.454545468,0.454545468};
  float4 fParam_DitherOffsetScale : packoffset(c11) = {0.00392156886,0.00392156886,-0.00392156886,0};
  float4 fParam_TonemapMaxMappingLuminance : packoffset(c12) = {1,1,1.015625,1};
  float4 fParam_BrightPass_LensDistortion : packoffset(c13);
  float4 afRGBA_Modulate[32] : packoffset(c14);
  float4 afRGBA_Offset[16] : packoffset(c46);
  float4 afUV_TexCoordOffsetV16[16] : packoffset(c62);
  float4x4 m44_ColorTransformMatrix : packoffset(c78);
  float4x4 m44_PreTonemapColorTransformMatrix : packoffset(c82);
  float4x4 m44_PreTonemapGlareColorTransformMatrix : packoffset(c86);
  float4 fParam_VignetteSimulate : packoffset(c90);
  float fParam_VignettePowerOfCosine : packoffset(c91);
  float4 afUVWQ_TexCoordScaleOffset[4] : packoffset(c92);
  float4 fParam_PerspectiveFactor : packoffset(c96);
  float fParam_FocusDistance : packoffset(c97);
  float4 fParam_DepthOfFieldConvertDepthFactor : packoffset(c98);
  float2 afXY_DepthOfFieldLevelBlendFactor16[16] : packoffset(c99);
  float fParam_DepthOfFieldLayerMaskThreshold : packoffset(c114.z) = {0.25};
  float fParam_DepthOfFieldFactorThreshold : packoffset(c114.w) = {0.00039999999};
  float4 afParam_TexCoordScaler8[8] : packoffset(c115);
  float4 fRGBA_Constant : packoffset(c123);
  float4 afRGBA_Constant[4] : packoffset(c124);
  float4x4 am44_TransformMatrix[8] : packoffset(c128);
  float4 afUV_TexCoordOffsetP32[96] : packoffset(c160);
}

SamplerState asamp2D_Texture_s : register(s0);
Texture2D<float4> atex2D_Texture : register(t0);

// TODO: these are meant to be float2 but it only works with float4??? Does this apply to all other shaders? Seems like it's only a problem here...? Actually that's right, the debug information was wrong
void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v1.xy, int2(0, 0)).xyzw;
  r0.x = GetLuminance(r0.xyz); // Luma: fixed BT.601 luminance (theoretically this game might have been BT.601, or some japanese bluish color space, but this remaster should have handled that already!)
  r1.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v3.xy, int2(0, 0)).xyzw;
  r0.y = GetLuminance(r1.xyz);
  r0.z = r0.x + r0.y;
  r1.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v2.xy, int2(0, 0)).xyzw;
  r0.w = GetLuminance(r1.xyz);
  r1.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v4.xy, int2(0, 0)).xyzw;
  r1.x = GetLuminance(r1.xyz);
  r1.y = r1.x + r0.w;
  r2.yw = -r1.yy + r0.zz;
  r0.z = r0.x + r0.w;
  r1.y = r1.x + r0.y;
  r1.y = -r1.y + r0.z;
  r0.z = r0.z + r0.y;
  r0.z = r0.z + r1.x;
  r0.z = 0.03125 * r0.z;
  r0.z = max(0.0078125, r0.z);
  r1.z = min(abs(r1.y), abs(r2.w));
  r2.xz = -r1.yy;
  r0.z = r1.z + r0.z;
  r0.z = 1 / r0.z;
  r2.xyzw = r2.xyzw * r0.zzzz;
  r2.xyzw = max(float4(-8,-8,-8,-8), r2.xyzw);
  r2.xyzw = min(float4(8,8,8,8), r2.xyzw);
  r2.xyzw = afRGBA_Modulate[0].xyxy * r2.xyzw;
  r3.xyzw = r2.xyzw * float4(-0.5,-0.5,0.5,0.5) + v0.xyxy;
  r2.xyzw = r2.zwzw * float4(-0.166666672,-0.166666672,0.166666672,0.166666672) + v0.xyxy;
  r4.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, r3.xy, int2(0, 0)).xyzw;
  r3.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, r3.zw, int2(0, 0)).xyzw;
  r1.yzw = r4.xyz + r3.xyz;
  r1.yzw = float3(0.25,0.25,0.25) * r1.yzw;
  r3.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, r2.xy, int2(0, 0)).xyzw;
  r2.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, r2.zw, int2(0, 0)).xyzw;
  r2.xyz = r3.xyz + r2.xyz;
  r1.yzw = r2.xyz * float3(0.25,0.25,0.25) + r1.yzw;
  r2.xyz = float3(0.5,0.5,0.5) * r2.xyz;
  r0.z = GetLuminance(r1.yzw);
  r2.w = min(r0.x, r0.w);
  r0.x = max(r0.x, r0.w);
  r0.w = min(r1.x, r0.y);
  r0.y = max(r1.x, r0.y);
  r0.x = max(r0.x, r0.y);
  r0.y = min(r2.w, r0.w);
  r3.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v0.xy, int2(0, 0)).xyzw;
  r0.w = GetLuminance(r3.xyz);
  o0.w = r3.w;
  r0.y = min(r0.w, r0.y);
  r0.x = max(r0.w, r0.x);
  r0.xy = (r0.xz < r0.zy);
  r0.x = asfloat(asint(r0.x) | asint(r0.y));
  o0.xyz = r0.x ? r2.xyz : r1.yzw;
}