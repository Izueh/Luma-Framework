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

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  r0.xyzw = atex2D_Texture.Sample(asamp2D_Texture_s, v0.xy, int2(0, 0)).xyzw;
  o0.xyzw = afRGBA_Modulate[0].xyzw * r0.xyzw;
}