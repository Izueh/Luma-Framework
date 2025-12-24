#include "../Includes/Common.hlsl"

cbuffer CBufCommonPerCamera : register(b2)
{
  float c_zNear : packoffset(c0);
  float3 c_cameraOrigin : packoffset(c0.y);
  row_major float4x4 c_cameraRelativeToClip : packoffset(c1);
  int c_frameNum : packoffset(c5);
  float3 c_cameraOriginPrevFrame : packoffset(c5.y);
  row_major float4x4 c_cameraRelativeToClipPrevFrame : packoffset(c6);
  float4 c_clipPlane : packoffset(c10);

  struct
  {
    float4 k0;
    float4 k1;
    float4 k2;
    float4 k3;
    float4 k4;
  } c_fogParams : packoffset(c11);

  float3 c_skyColor : packoffset(c16);
  float c_shadowBleedFudge : packoffset(c16.w);
  float c_envMapLightScale : packoffset(c17);
  float3 c_sunColor : packoffset(c17.y);
  float3 c_sunDir : packoffset(c18);
  float c_gameTime : packoffset(c18.w);

  struct
  {
    float3 shadowRelConst;
    bool enableShadows;
    float3 shadowRelForX;
    float unused_1;
    float3 shadowRelForY;
    float cascadeWeightScale;
    float3 shadowRelForZ;
    float cascadeWeightBias;
    float4 laterCascadeScale;
    float4 laterCascadeBias;
    float2 normToAtlasCoordsScale0;
    float2 normToAtlasCoordsBias0;
    float4 normToAtlasCoordsScale12;
    float4 normToAtlasCoordsBias12;
  } c_csm : packoffset(c19);

  uint c_lightTilesX : packoffset(c28);
  float c_minShadowVariance : packoffset(c28.y);
  float2 c_renderTargetSize : packoffset(c28.z);
  float2 c_rcpRenderTargetSize : packoffset(c29);
  float c_numCoverageSamples : packoffset(c29.z);
  float c_rcpNumCoverageSamples : packoffset(c29.w);
  float2 c_cloudRelConst : packoffset(c30);
  float2 c_cloudRelForX : packoffset(c30.z);
  float2 c_cloudRelForY : packoffset(c31);
  float2 c_cloudRelForZ : packoffset(c31.z);
  float c_sunHighlightSize : packoffset(c32);
  uint c_globalLightingFlags : packoffset(c32.y);
  uint c_useRealTimeLighting : packoffset(c32.z);
  float c_forceExposure : packoffset(c32.w);
  int c_debugInt : packoffset(c33);
  float c_debugFloat : packoffset(c33.y);
  float c_maxLightingValue : packoffset(c33.z);
  float c_viewportMaxZ : packoffset(c33.w);
  float2 c_viewportScale : packoffset(c34);
  float2 c_rcpViewportScale : packoffset(c34.z);
  float2 c_framebufferViewportScale : packoffset(c35);
  float2 c_rcpFramebufferViewportScale : packoffset(c35.z);
}

cbuffer CBufEnginePost : register(b1)
{
  float c_bloomAmount : packoffset(c0);
  float3 c_viewFadeScale : packoffset(c0.y);
  float3 c_viewFadeBias : packoffset(c1);
  float c_fadeToBlackFactor : packoffset(c1.w);
  float3 c_colorCorrectionVolumeWeights : packoffset(c2);
  float c_desaturationLerp : packoffset(c2.w);
  float2 c_desaturationExcludeNormalizedCBCR : packoffset(c3);
  float c_desaturationExcludeDotRangeBegin : packoffset(c3.z);
  float c_desaturationExcludeDotRangeDeltaInverse : packoffset(c3.w);
  float2 c_cloakAberrationScale : packoffset(c4);
  float c_cloakBrightenScale : packoffset(c4.z);
  float c_cloakBrightenBias : packoffset(c4.w);
  float3 c_cloakChromaTints : packoffset(c5);
  float c_cloakDesaturate : packoffset(c5.w);
  float c_lcd_pixelScaleX1 : packoffset(c6);
  float c_lcd_pixelScaleX2 : packoffset(c6.y);
  float c_lcd_pixelScaleY : packoffset(c6.z);
  float c_lcd_brightness : packoffset(c6.w);
  float c_lcd_contrast : packoffset(c7);
  float c_lcd_wave_scale : packoffset(c7.y);
  float c_lcd_wave_offset : packoffset(c7.z);
  float c_lcd_wave_speed : packoffset(c7.w);
  float c_lcd_wave_period : packoffset(c8);
  float c_lcd_bloom_add : packoffset(c8.y);
  float c_lcd_pixelflicker : packoffset(c8.z);
  uint c_lcd_flags : packoffset(c8.w);
  float c_phase_baseScale : packoffset(c9);
  float c_phase_bloomScale : packoffset(c9.y);
  uint c_phase_flags : packoffset(c9.z);
  float c_toolTime : packoffset(c9.w);
  float c_noiseOffset : packoffset(c10);
  float c_noiseScale : packoffset(c10.y);
  float c_bloomExponentPost : packoffset(c10.z);
  float c_wideBloomAmount : packoffset(c10.w);
  float c_wideBloomExponentPost : packoffset(c11);
  float c_streakBloomAmount : packoffset(c11.y);
  float c_streakBloomExponentPost : packoffset(c11.z);
  float c_sharpenAmount : packoffset(c11.w);
  float c_sharpenWidth : packoffset(c12);
  float c_sharpenThreshold : packoffset(c12.y);
  float c_debugTonemapEnableTweaks : packoffset(c12.z);
  float c_debugTonemapDisable : packoffset(c12.w);
  float c_debugTonemapToe : packoffset(c13);
  float c_debugTonemapMid1 : packoffset(c13.y);
  float c_debugTonemapMid2 : packoffset(c13.z);
  float c_debugTonemapShoulder : packoffset(c13.w);
  float2 c_postprocessUVScale : packoffset(c14);
  float2 c_postprocessMaxUVs : packoffset(c14.z);
  float2 c_postprocessTinyUVScale : packoffset(c15);
  float2 c_postprocessTinyMaxUVs : packoffset(c15.z);
  float2 c_fbMaxUVs : packoffset(c16);
  int2 c_fbMaxScreenPos : packoffset(c16.z);

  struct
  {
    float nearDepthEnd;
    float3 unused3;
    float4 worldParams;
  } c_dof : packoffset(c17);

}

SamplerState BaseTextureSampler_s : register(s0);
SamplerState FBTextureSampler_s : register(s1);
SamplerState ColorCorrectionVolumeTexture0Sampler_s : register(s3);
SamplerState WideBloomTextureSampler_s : register(s8);
SamplerState StreakBloomTextureSampler_s : register(s9);
SamplerState DoFBlurSmallTextureSampler_s : register(s10);
Texture2D<float4> BaseTexture : register(t0);
Texture2D<float4> FBTexture : register(t1);
Texture3D<float4> ColorCorrectionVolumeTexture0 : register(t3);
Texture2D<float4> WideBloomTexture : register(t8);
Texture2D<float4> StreakBloomTexture : register(t9);
Texture2D<float4> DoFBlurSmallTexture : register(t10);
Texture2D<float4> CoCTexture : register(t11);
Texture2D<float4> exposureTexture : register(t30);

#define cmp

void main(
  float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;
  int4 r1i;
  r0.xy = min(c_postprocessMaxUVs.xy, v0.xy);
  r0.xyz = BaseTexture.Sample(BaseTextureSampler_s, r0.xy, int2(0, 0)).xyz;
  r1i.xy = v1.xy; // ftoi
  r1i.zw = 0;
  r2.xyzw = FBTexture.Load(r1i.xyw).xyzw;
#if 1
  o0.a = 1;
  o0.xyz = r2.xyz;
  return;
#endif
  r0.w = dot(r2.xyz, float3(0.298999995,0.587000012,0.114));
  r3.xy = c_sharpenWidth * c_rcpRenderTargetSize.xy;
  r3.xy = r3.xy / c_framebufferViewportScale.xy;
  r3.zw = v0.xy + r3.xy;
  r3.zw = min(c_fbMaxUVs.xy, r3.zw);
  r4.xyz = FBTexture.Sample(FBTextureSampler_s, r3.zw).xyz;
  r3.z = dot(r4.xyz, float3(0.298999995,0.587000012,0.114));
  r4.xyzw = r3.xyxy * float4(-1,1,1,-1) + v0.xyxy;
  r4.xyzw = min(c_fbMaxUVs.xyxy, r4.xyzw);
  r5.xyz = FBTexture.Sample(FBTextureSampler_s, r4.xy).xyz;
  r3.w = dot(r5.xyz, float3(0.298999995,0.587000012,0.114));
  r3.xy = v0.xy + -r3.xy;
  r3.xy = min(c_fbMaxUVs.xy, r3.xy);
  r5.xyz = FBTexture.Sample(FBTextureSampler_s, r3.xy).xyz;
  r3.x = dot(r5.xyz, float3(0.298999995,0.587000012,0.114));
  r4.xyz = FBTexture.Sample(FBTextureSampler_s, r4.zw).xyz;
  r3.y = dot(r4.xyz, float3(0.298999995,0.587000012,0.114));
  r4.x = min(r3.z, r0.w);
  r4.x = min(r4.x, r3.w);
  r4.x = min(r4.x, r3.x);
  r4.x = min(r4.x, r3.y);
  r4.y = max(r3.z, r0.w);
  r4.y = max(r4.y, r3.w);
  r4.y = max(r4.y, r3.x);
  r4.y = max(r4.y, r3.y);
  r4.y = 0.00100000005 + r4.y;
  r4.x = r4.x / r4.y;
  r4.x = cmp(c_sharpenThreshold < r4.x);
  r3.z = r3.z + r3.w;
  r3.x = r3.z + r3.x;
  r3.x = r3.x + r3.y;
  r3.x = r3.x * 0.25 + -r0.w;
  r3.x = -r3.x * c_sharpenAmount + r0.w;
  r0.w = 0.00100000005 + r0.w;
  r0.w = r3.x / r0.w;
  r3.xyz = r2.xyz * r0.www;
  r2.xyz = r4.xxx ? r3.xyz : r2.xyz;
  r0.w = 1 + -r2.w;
  r2.w = cmp(0 < r0.w);
  if (r2.w != 0) {
    r3.xy = c_cloakAberrationScale.xy * r0.ww;
    r4.xyzw = float4(1,0,-0.5,-0.866029978) * r3.xyxy;
    r4.xyzw = (int4)r4.xyzw;
    r3.xy = float2(-0.5,0.866029978) * r3.xy;
    r3.xy = (int2)r3.xy;
    r4.xyzw = r1i.xyxy + (int4)r4.xyzw;
    r4.xyzw = min((int4)c_fbMaxScreenPos.xyxy, (int4)r4.zwxy);
    r5.xy = r4.zw;
    r5.zw = float2(0,0);
    r5.xyz = FBTexture.Load(r5.xyz).xyz;
    r4.zw = float2(0,0);
    r4.xyz = FBTexture.Load(r4.xyz).xyz;
    r3.xy = r1i.xy + (int2)r3.xy;
    r3.xy = min((int2)c_fbMaxScreenPos.xy, (int2)r3.xy);
    r3.zw = float2(0,0);
    r3.xyz = FBTexture.Load(r3.xyz).xyz;
    r4.xyz = c_cloakChromaTints.yyy * r4.xyz;
    r4.xyz = r5.xyz * c_cloakChromaTints.xxx + r4.xyz;
    r3.xyz = r3.xyz * c_cloakChromaTints.zzz + r4.xyz;
    r3.xyz = r3.xyz * c_cloakBrightenScale + c_cloakBrightenBias;
    r2.w = dot(r3.xyz, float3(0.298999995,0.587000012,0.114));
    r4.xyz = r2.www + -r3.xyz;
    r3.xyz = c_cloakDesaturate * r4.xyz + r3.xyz;
    r0.w = saturate(r0.w);
    r3.xyz = r3.xyz + -r2.xyz;
    r2.xyz = r0.www * r3.xyz + r2.xyz;
  }
  r3.xy = -c_rcpRenderTargetSize.xy + v0.xy;
  r3.xy = min(c_postprocessMaxUVs.xy, r3.xy);
  r3.xyzw = DoFBlurSmallTexture.Sample(DoFBlurSmallTextureSampler_s, r3.xy, int2(0, 0)).xyzw;
  r4.xy = c_rcpRenderTargetSize.xy + v0.xy;
  r4.xy = min(c_postprocessMaxUVs.xy, r4.xy);
  r4.xyzw = DoFBlurSmallTexture.Sample(DoFBlurSmallTextureSampler_s, r4.xy, int2(0, 0)).xyzw;
  r3.xyzw = r4.xyzw + r3.xyzw;
  r0.w = 0.5 * r3.w;
  r1.x = CoCTexture.Load(r1i.xyz).x;
  r1.x = max(0, r1.x);
  r0.w = max(r1.x, abs(r0.w));
  r0.xyzw = float4(0.5,0.5,0.5,4) * r0.xyzw;
  r0.w = min(1, r0.w);
  r1.xyz = r3.xyz * float3(0.5,0.5,0.5) + -r2.xyz;
  r1.xyz = r0.www * r1.xyz + r2.xyz;
  r2.xy = min(c_postprocessTinyMaxUVs.xy, v0.xy);
  r2.xyz = WideBloomTexture.Sample(WideBloomTextureSampler_s, r2.xy, int2(0, 0)).xyz;
  r3.xyz = StreakBloomTexture.Sample(StreakBloomTextureSampler_s, v0.xy, int2(0, 0)).xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = c_bloomExponentPost * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.xyz = c_bloomAmount * r0.xyz + r1.xyz;
  r1.xyz = log2(abs(r2.xyz));
  r1.xyz = c_wideBloomExponentPost * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r0.xyz = c_wideBloomAmount * r1.xyz + r0.xyz;
  r1.xyz = log2(abs(r3.xyz));
  r1.xyz = c_streakBloomExponentPost * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r0.xyz = c_streakBloomAmount * r1.xyz + r0.xyz;
  r0.w = cmp(0 < c_forceExposure);
  if (r0.w != 0) {
    r0.w = c_forceExposure;
  } else {
    r0.w = exposureTexture.Load(0).x;
  }
  r0.xyz = r0.xyz * r0.www;
  r1.xy = cmp(float2(0,0) != c_debugTonemapDisable);
  r2.xyz = c_debugTonemapMid1 * r0.xyz + c_debugTonemapToe;
  r2.xyz = r2.xyz * r0.xyz;
  r3.xyz = c_debugTonemapMid1 * r0.xyz + c_debugTonemapShoulder;
  r3.xyz = r0.xyz * r3.xyz + c_debugTonemapMid2;
  r3.xyz = rcp(r3.xyz);
  r2.xyz = r3.xyz * r2.xyz;
  r3.xyz = r0.xyz * float3(10,10,10) + float3(0.300000012,0.300000012,0.300000012);
  r3.xyz = r3.xyz * r0.xyz;
  r4.xyz = r0.xyz * float3(10,10,10) + float3(0.5,0.5,0.5);
  r4.xyz = r0.xyz * r4.xyz + float3(1.5,1.5,1.5);
  r4.xyz = rcp(r4.xyz);
  r3.xyz = r4.xyz * r3.xyz;
  r1.yzw = r1.yyy ? r2.xyz : r3.xyz;
  r0.xyz = r1.xxx ? r0.xyz : r1.yzw;
  r0.xyz = r0.xyz * c_viewFadeScale.xyz + c_viewFadeBias.xyz;
  
  r1.xyz = linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR);
#if 0
  r1.xyz = r1.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r1.xyz = ColorCorrectionVolumeTexture0.Sample(ColorCorrectionVolumeTexture0Sampler_s, r1.xyz).xyz;
#else
  r1.xyz = gamma_sRGB_to_linear(r1.xyz, GCT_MIRROR);
#endif

  r0.w = saturate(1 - c_colorCorrectionVolumeWeights.x);
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r1.xyz * c_colorCorrectionVolumeWeights.xxx + r0.xyz;
  o0.xyz = c_fadeToBlackFactor * r0.xyz;
  o0.w = 1;
}