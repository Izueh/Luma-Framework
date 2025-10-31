#include "Includes/Common.hlsl"
#include "../Includes/Oklab.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer _Globals : register(b0)
{
  float Canvas_hlsl_PSMain00000000000000000000000000000001_1bits : packoffset(c0) = {0};
  float4 fogColor : packoffset(c1);
  float3 fogTransform : packoffset(c2);
  float2 fogLuminance : packoffset(c3);
  row_major float3x4 screenDataToCamera : packoffset(c4);
  float globalScale : packoffset(c7);
  float sceneDepthAlphaMask : packoffset(c7.y);
  float globalOpacity : packoffset(c7.z);
  float distortionBufferScale : packoffset(c7.w);
  float3 wToZScaleAndBias : packoffset(c8);
  float4 screenTransform[2] : packoffset(c9);
  row_major float4x4 worldViewProj : packoffset(c11);
  float3 localEyePos : packoffset(c15);
  float4 vertexClipPlane : packoffset(c16);
  float ViewX : packoffset(c17);
  float ViewY : packoffset(c17.y);
  float ViewZ : packoffset(c17.z);
  float NormalX : packoffset(c17.w);
  float NormalY : packoffset(c18);
  float NormalZ : packoffset(c18.y);
  float4 globalColor : packoffset(c19);
}

SamplerState s_sceneDepth_s : register(s0);
Texture2D<float> s_sceneDepth : register(t0);
Texture2D<float4> sceneTexture : register(t1); // Copy of the last scene set by Luma

float4 GetAdditiveFogByDepth(float depth, float2 depthUV, float4 fogOpacity, float4 fogIntensity)
{
  float4 r0,r1;
  r0.x = 1 + -wToZScaleAndBias.z;
  r0.w = (depth >= 0.01) ? 1.0 : 0; // No fog below 0.01?
  r0.x = r0.w * r0.x + wToZScaleAndBias.z;
  r0.x = depth * r0.x + -wToZScaleAndBias.x;
  r1.z = wToZScaleAndBias.y / r0.x;
  r1.xy = r1.zz * depthUV;
  r1.w = 1;
  r0.x = dot(screenDataToCamera._m00_m01_m02_m03, r1.xyzw);
  r0.y = dot(screenDataToCamera._m10_m11_m12_m13, r1.xyzw);
  r0.z = dot(screenDataToCamera._m20_m21_m22_m23, r1.xyzw);
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = r0.x * fogTransform.x + fogTransform.y;
  r0.x = 1.44269502 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = min(1, r0.x);
  r0.x = -fogColor.w * r0.x + fogColor.w;
  r0.x = fogTransform.z * r0.x;
  r0.x = fogIntensity.w * r0.x;
  r0.y = globalOpacity * fogOpacity.w;
  float4 outColor;
  outColor.w = r0.x * r0.y;
  r0.xyz = fogColor.xyz * fogIntensity.xyz;
  outColor.xyz = globalScale * r0.xyz;
  return outColor;
}

void main(
  float4 v0 : TEXCOORD6,
  float4 fogOpacity : TEXCOORD7,
  float4 fogIntensity : COLOR0,
  out float4 outColor : SV_Target0)
{
  const float2 depthUV = v0.xy / v0.ww;
  const float depth = s_sceneDepth.Sample(s_sceneDepth_s, depthUV).x;
  float4 additiveFog = GetAdditiveFogByDepth(depth, depthUV, fogOpacity, fogIntensity);

  additiveFog.a = pow(additiveFog.a, 1.0 / LumaSettings.GameSettings.FogIntensity); // Division by 0 goes +INF
  //additiveFog.a *= LumaSettings.GameSettings.FogIntensity; // Doesn't properly work > 1

#if ENABLE_LUMA // LUMA: Fix fog to avoid it raising blacks

  //additiveFog.rgba = max(additiveFog.rgba - (GetAdditiveFogByDepth(0.0, depthUV, fogOpacity, fogIntensity).rgba * (1.0 - pow(depth, 125.0))), 0.0); // Attempted change

  // Pre-blend with the background (straight alpha, as the state was set to) and force the alpha to 1 to fully control the result (we can't fix the fact that it raises blacks without knowing the background color)
  const float3 backgroundColor = sceneTexture.Sample(s_sceneDepth_s, depthUV).rgb; // Added by luma
  float4 sceneWithFog = float4(lerp(backgroundColor.rgb, additiveFog.rgb, additiveFog.a), 1.0);

  // Restore the original luminance near the camera, to fog can only make the scene brighter further in the distance
#if 1
  float3 prevSceneWithFog = sceneWithFog.rgb;
  float3 backgroundOklab = Oklab::linear_srgb_to_oklab(backgroundColor.rgb);
  float3 sceneWithFogOklab = Oklab::linear_srgb_to_oklab(sceneWithFog.rgb);
  //float3 fogOklab = Oklab::linear_srgb_to_oklab(additiveFog.rgb);

  // Start from the non fogged scene background and restore some of the fogged scene brightness in the distance (not close to the camera, to avoid raised blacks)
  backgroundOklab.x = lerp(backgroundOklab.x, sceneWithFogOklab.x, pow(saturate(depth), 33.333)); // Heuristically found value (hopefully the depth far plane is consistent through the game)
  float3 backgroundColorWithFogBrightness = Oklab::oklab_to_linear_srgb(backgroundOklab);
  
  // Restore the fog hue and chrominance, to indeed have it look similar to vanilla
  const float fogSaturation = 1.0; // Values beyond 0.7 and 0.9 make the fog look a bit closer to vanilla, without raising blacks, but it looks nicer with extra saturation and goes into BT.2020
  sceneWithFog.rgb = RestoreHueAndChrominance(backgroundColorWithFogBrightness, sceneWithFog.rgb, 1.0, fogSaturation); // I'm a bit confused as to why but if we restore any less hue than 1, it looks either broken or bad

  sceneWithFog.rgb = lerp(prevSceneWithFog, sceneWithFog.rgb, LumaSettings.GameSettings.FogCorrectionIntensity);
#else // Alternative older version, it's not as accurate to the vanilla fog not as good looking as the oklab version
  float3 correctedColor = RestoreLuminance(sceneWithFog.rgb, backgroundColor.rgb, true);
  float correctionRatio = 1.0 - saturate(GetLuminance(correctedColor) / GetLuminance(sceneWithFog.rgb));
  // Re-tint the fog by adding the additive color again, and the restoring the luminance again (there might be a way to do it with simpler math but whatever)
  correctedColor = RestoreLuminance(correctedColor + (additiveFog.rgb * additiveFog.a * pow(correctionRatio, 0.333) * 3.333), correctedColor, true);
  correctedColor = lerp(sceneWithFog.rgb, correctedColor, LumaSettings.GameSettings.FogCorrectionIntensity);
  sceneWithFog.rgb = lerp(correctedColor, sceneWithFog.rgb, pow(depth, 66.666)); // Heuristically found value (hopefully the depth far plane is consistent through the game)
  //sceneWithFog.rgb = lerp(GetLuminance(sceneWithFog.rgb), sceneWithFog.rgb, 1.0 + (1.0 - pow(depth, 33.333)));
#endif

  outColor = sceneWithFog;

#else

  outColor = additiveFog;

#endif
}