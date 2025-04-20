#pragma once

#include "..\..\..\Core\includes\math.h"
#include "..\..\..\Core\includes\matrix.h"

namespace
{
   // See shaders for comments on these
   struct CBPerViewGlobal
   {
      Matrix44F  CV_ViewProjZeroMatr;
      float4    CV_AnimGenParams;

      Matrix44F  CV_ViewProjMatr;
      Matrix44F  CV_ViewProjNearestMatr;
      Matrix44F  CV_InvViewProj;
      Matrix44F  CV_PrevViewProjMatr;
      Matrix44F  CV_PrevViewProjNearestMatr;
      /*Matrix34A*/ float4 CV_ScreenToWorldBasis[3];
      float4    CV_TessInfo;
      float4    CV_CameraRightVector;
      float4    CV_CameraFrontVector;
      float4    CV_CameraUpVector;

      float4    CV_ScreenSize;
      float4    CV_HPosScale;
      float4    CV_HPosClamp;
      float4    CV_ProjRatio;
      float4    CV_NearestScaled;
      float4    CV_NearFarClipDist;

      float4    CV_SunLightDir;
      float4    CV_SunColor;
      float4    CV_SkyColor;
      float4    CV_FogColor;
      float4    CV_TerrainInfo;

      float4    CV_DecalZFightingRemedy;

      Matrix44F  CV_FrustumPlaneEquation;

      float4    CV_WindGridOffset;

      Matrix44F  CV_ViewMatr;
      Matrix44F  CV_InvViewMatr;

      float     CV_LookingGlass_SunSelector;
      float     CV_LookingGlass_DepthScalar;

      float     CV_PADDING0;
      float     CV_PADDING1;
   };
   constexpr uint32_t CBPerViewGlobal_buffer_size = 1024; // This is how much CryEngine allocates for buffers that hold this (it doesn't use "sizeof(CBPerViewGlobal)")
   static_assert(CBPerViewGlobal_buffer_size > sizeof(CBPerViewGlobal));

   struct CBPerFrame
   {
      Matrix44F  CF_ShadowSampling_TexGen0;
      Matrix44F  CF_ShadowSampling_TexGen1;
      Matrix44F  CF_ShadowSampling_TexGen2;
      Matrix44F  CF_ShadowSampling_TexGen3;

	   float4    CF_ShadowSampling_InvShadowMapSize;
	   float4    CF_ShadowSampling_DepthTestBias; // defines how hard depth test is (default is 100 - hard test)
	   float4    CF_ShadowSampling_OneDivFarDist;
	   float4    CF_ShadowSampling_KernelRadius;

	   float4    CF_VolumetricFogParams;
	   float4    CF_VolumetricFogRampParams;
	   float4    CF_VolumetricFogSunDir;
	   float4    CF_FogColGradColBase;
	   float4    CF_FogColGradColDelta;
	   float4    CF_FogColGradParams;
	   float4    CF_FogColGradRadial;

	   float4    CF_VolumetricFogSamplingParams;
	   float4    CF_VolumetricFogDistributionParams;
	   float4    CF_VolumetricFogScatteringParams;
	   float4    CF_VolumetricFogScatteringBlendParams;
	   float4    CF_VolumetricFogScatteringColor;
	   float4    CF_VolumetricFogScatteringSecondaryColor;
	   float4    CF_VolumetricFogHeightDensityParams;
	   float4    CF_VolumetricFogHeightDensityRampParams;
	   float4    CF_VolumetricFogDistanceParams;

	   float4    CF_VolumetricFogGlobalEnvProbe0;
	   float4    CF_VolumetricFogGlobalEnvProbe1;

	   float4    CF_CloudShadingColorSun;
	   float4    CF_CloudShadingColorSky;

	   float     CF_SSDOAmountDirect;
	   float3    __padding0;
	
      float4    CF_Timers[4]; // ETIMER_LAST
	   float4    CF_RandomNumbers; //TODOFT5: intercept these buffers and replace the random numbers generation (4 0-1 floats) from running at the game full refresh rate to running at 24 or 30 or 60 fps, as some objects (e.g. glass and monitors) generate grain based on it, and it's not visible at higher frame rates (and it's also not visible with DLSS, so ideally these objects would write in the DLSS bias buffer too!)

	   float4    CF_irreg_kernel_2d[8];
   };
}