struct FrameColor_s
{
    uint3 rgbSUM;                  // Offset:    0
    float3 averageColor;           // Offset:   12
};

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

cbuffer CBufUberStatic : register(b0)
{
  float2 c_uv1RotScaleX : packoffset(c0);
  float2 c_uv1RotScaleY : packoffset(c0.z);
  float2 c_uv1Translate : packoffset(c1);
  float2 c_uv2RotScaleX : packoffset(c1.z);
  float2 c_uv2RotScaleY : packoffset(c2);
  float2 c_uv2Translate : packoffset(c2.z);
  float2 c_uv3RotScaleX : packoffset(c3);
  float2 c_uv3RotScaleY : packoffset(c3.z);
  float2 c_uv3Translate : packoffset(c4);
  float2 c_uvDistortionIntensity : packoffset(c4.z);
  float2 c_uvDistortion2Intensity : packoffset(c5);
  float c_fogColorFactor : packoffset(c5.z);
  float c_layerBlendRamp : packoffset(c5.w);
  float3 c_albedoTint : packoffset(c6);
  float c_opacity : packoffset(c6.w);
  float c_useAlphaModulateSpecular : packoffset(c7);
  float c_alphaEdgeFadeExponent : packoffset(c7.y);
  float c_alphaEdgeFadeInner : packoffset(c7.z);
  float c_alphaEdgeFadeOuter : packoffset(c7.w);
  float c_useAlphaModulateEmissive : packoffset(c8);
  float c_emissiveEdgeFadeExponent : packoffset(c8.y);
  float c_emissiveEdgeFadeInner : packoffset(c8.z);
  float c_emissiveEdgeFadeOuter : packoffset(c8.w);
  float c_alphaDistanceFadeScale : packoffset(c9);
  float c_alphaDistanceFadeBias : packoffset(c9.y);
  float c_alphaTestReference : packoffset(c9.z);
  float c_aspectRatioMulV : packoffset(c9.w);
  float3 c_emissiveTint : packoffset(c10);
  float c_shadowBias : packoffset(c10.w);
  float c_tsaaDepthAlphaThreshold : packoffset(c11);
  float c_tsaaMotionAlphaThreshold : packoffset(c11.y);
  float c_tsaaMotionAlphaRamp : packoffset(c11.z);
  uint c_tsaaResponsiveFlag : packoffset(c11.w);
  float c_dofOpacityLuminanceScale : packoffset(c12);
  float c_glitchStrength : packoffset(c12.y);
  float2 pad_CBufUberStatic : packoffset(c12.z);
  float c_perfGloss : packoffset(c13);
  float3 c_perfSpecColor : packoffset(c13.y);
}

cbuffer CBufUberDynamic : register(b1)
{
  uint c_useDitherFade : packoffset(c0);
  float c_vsmScale : packoffset(c0.y);
  float c_glitchAberrationScale : packoffset(c0.z);
  float c_rcpCloakAberrationScale : packoffset(c0.w);

  struct
  {
    float nearDepthEnd;
    float3 unused3;
    float4 worldParams;
  } c_dof : packoffset(c1);

}

SamplerState allSamplers_2__s : register(s2);
Texture2D<float4> albedoTexture : register(t0);
Texture2D<float4> emissiveTexture : register(t4);
StructuredBuffer<FrameColor_s> frameColor : register(t41);


// 3Dmigoto declarations
#define cmp -


void main(
  linear centroid float4 v0 : TEXCOORD0,
  linear centroid float4 v1 : TEXCOORD4,
  linear centroid float4 v2 : TEXCOORD6,
  float4 v3 : SV_Position0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2)
{
  float4 r0,r1,r2;
  r0.x = c_cameraOrigin.z + v1.z;
  r0.y = max(c_cameraOrigin.z, r0.x);
  r0.x = min(c_cameraOrigin.z, r0.x);
  r0.y = r0.y + -r0.x;
  r0.z = max(9.99999975e-006, r0.y);
  r0.y = saturate(c_fogParams.k4.z * r0.y);
  r0.y = c_fogParams.k0.z * r0.y;
  r0.z = 1 / r0.z;
  r1.xy = c_fogParams.k4.xy + -r0.xx;
  r0.x = -c_fogParams.k4.x + r0.x;
  r0.x = saturate(c_fogParams.k4.z * r0.x);
  r0.x = c_fogParams.k0.z * r0.x + c_fogParams.k0.x;
  r0.zw = saturate(r1.xy * r0.zz);
  r1.x = dot(v1.xyz, v1.xyz);
  r1.y = sqrt(r1.x);
  r1.x = rsqrt(r1.x);
  r1.xzw = v1.xyz * r1.xxx;
  r1.x = dot(c_fogParams.k3.xyz, -r1.xzw);
  r1.x = -c_fogParams.k2.w + -r1.x;
  r1.x = saturate(c_fogParams.k3.w * r1.x);
  r1.x = r1.x * r1.x;
  r1.xzw = c_fogParams.k2.xyz * r1.xxx + c_fogParams.k1.xyz;
  r1.y = c_fogParams.k1.w + r1.y;
  r1.y = max(9.99999975e-006, r1.y);
  r0.z = r1.y * r0.z;
  r2.x = r1.y * r0.w + -r0.z;
  r0.w = -r1.y * r0.w + r1.y;
  r0.y = r0.y / r1.y;
  r0.y = r0.y * r2.x;
  r0.x = r0.y * 0.5 + r0.x;
  r0.y = c_fogParams.k0.y * r0.w;
  r0.y = c_fogParams.k0.x * r0.z + r0.y;
  r0.x = r0.x * r2.x + r0.y;
  r0.x = exp2(-r0.x);
  r0.x = 1 + -r0.x;
  r0.x = c_fogParams.k0.w * r0.x;
  r0.yzw = albedoTexture.Sample(allSamplers_2__s, v0.xy).xyz;
  r0.yzw = c_albedoTint.xyz * r0.yzw;
  r2.x = frameColor[0].averageColor.x;
  r2.y = frameColor[0].averageColor.y;
  r2.z = frameColor[0].averageColor.z;
  r0.yzw = r2.xyz * r0.yzw;
  r1.y = max(r0.z, r0.w);
  r1.y = max(r1.y, r0.y);
  r0.yzw = c_maxLightingValue * r0.yzw;
  r1.y = max(c_maxLightingValue, r1.y);
  r1.y = rcp(r1.y);
  r2.xyz = emissiveTexture.Sample(allSamplers_2__s, v0.xy).xyz;
  r2.xyz = c_emissiveTint.xyz * r2.xyz;
  r0.yzw = r0.yzw * r1.yyy + r2.xyz;
  r1.xyz = r1.xzw * c_fogColorFactor + -r0.yzw;
  o0.xyz = r0.xxx * r1.xyz + r0.yzw;
  o0.w = 1;
  r0.xy = v2.xy / v2.ww;
  r0.xy = r0.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r0.zw = c_rcpRenderTargetSize.xy * v3.xy;
  r0.xy = r0.zw * c_rcpViewportScale.xy + -r0.xy;
  r0.xy = float2(1024,1024) * r0.xy;
  r0.y = (int)r0.y & 0xffff8000;
  o1.x = r0.x;
  o1.y = (int)r0.y | c_tsaaResponsiveFlag;
  o1.zw = float2(0,1);
  r0.x = c_viewportMaxZ + -v3.z;
  r0.x = c_zNear / r0.x;
  r0.y = cmp(r0.x < c_dof.nearDepthEnd);
  r0.zw = r0.yy ? c_dof.worldParams.xy : c_dof.worldParams.zw;
  r0.y = r0.y ? 1.000000 : 0;
  r0.x = saturate(r0.x * r0.z + r0.w);
  r0.y = r0.x * r0.y;
  o2.xyzw = r0.yyyy * float4(-2,-2,-2,-2) + r0.xxxx;
}