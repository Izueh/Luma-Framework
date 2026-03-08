#include "Includes/Common.hlsl"

cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

cbuffer MotionBlurFrameConsts : register(b3)
{
  row_major float4x4 PrevViewProjMatrix : packoffset(c0); // Jittered
  float2 LinearDepthParams : packoffset(c4);
  float FarZ : packoffset(c4.z);
  int CameraMotionBlurOnly : packoffset(c4.w);
  float MaxBlurRadius : packoffset(c5);
  float ExposureTime : packoffset(c5.y);
  float MaxBlurRadiusLength : packoffset(c5.z);
  float EpsilonRadiusLength : packoffset(c5.w);
  float PixelLength : packoffset(c6);
  float HalfPixelLength : packoffset(c6.y);
  float2 HalfPixelSize : packoffset(c6.z);
  float VarianceThresholdLength : packoffset(c7);
  int SampleCount : packoffset(c7.y);
  int UseMotionLOD : packoffset(c7.z);
  float AspectRatio : packoffset(c7.w);
  float RadialBlurOffset : packoffset(c8);
  float RadialBlurFactor : packoffset(c8.y);
  float2 RadialBlurPosXY : packoffset(c8.z);
  int ScreenWidth : packoffset(c9);
  int ScreenHeight : packoffset(c9.y);
  float CenterSampleWeight : packoffset(c9.z);
  int pad1 : packoffset(c9.w);
  float2 UVScale : packoffset(c10);
}

#ifndef FIX_MOTION_BLUR_SHUTTER_SPEED
#define FIX_MOTION_BLUR_SHUTTER_SPEED 0
#endif

SamplerState PointSampler_s : register(s1);
Texture2D<float4> VelocityTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

// Merges and decodes MVs
void main(
  float4 v0 : SV_Position0,
  float2 uvJittered : TEXCOORD0,
  float2 uvNonJittered : TEXCOORD1,
  float3 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0
#if _A1037803
  , out float2 o1 : SV_Target1
#endif
  )
{
  float4 r1,r2;
  float depth = DepthTexture.Load(int3(v0.xy, 0)).x; // Jittered depth
  float linearDepth = depth * LinearDepthParams.x + LinearDepthParams.y;
  float invLinearDepth = 1 / linearDepth;
  r1.xyz = invLinearDepth * v2.xyz + Globals[4].xyz; // Add camera/world position? This is jittered (due to Luma's changes)
  // The previous proj matrix would have been jittered!
  r2.xyz = r1.y * PrevViewProjMatrix._m10_m11_m13;
  r1.xyw = r1.x * PrevViewProjMatrix._m00_m01_m03 + r2.xyz;
  r1.xyz = r1.z * PrevViewProjMatrix._m20_m21_m23 + r1.xyw;
  r1.xyz = PrevViewProjMatrix._m30_m31_m33 + r1.xyz;
  float2 prevNDC = r1.xy / r1.z;
  float2 prevUV = prevNDC * float2(0.5,-0.5) + float2(0.5,0.5); // NDC to UV space

  // Dejitter, we don't want MVs to be jittered for Motion Blur nor TAA:
  {
    // Remove jitters of the previous frame, baked in "PrevViewProjMatrix".
    // Note: disabled as it actually makes things worse, I don't know why, but it works fine without this, jitters are accurate enough.
    //prevUV -= LumaData.GameData.PrevJitters;

    // Compensate the jitter added from the vertex shader
    prevUV -= LumaData.GameData.CurrJitters;
  }

  float2 cameraMotionVectors = uvNonJittered.xy - prevUV; // In UV space
  float2 finalMotionVectors = cameraMotionVectors;
  bool isDynamicObject = false;

  if (CameraMotionBlurOnly == 0) {
    float3 velocity = VelocityTexture.Sample(PointSampler_s, uvNonJittered.xy).xyz; // Maybe it'd be better to use "uvJittered" with linear sampling but this is likely totally fine
    float2 uvVelocity = velocity.xy * 2.0 - 1.0; // Decode from 0|1 to -1|+1 ()
    float2 dynamicObjectsMotionVectors = float2(0.125,0.125) * uvVelocity; // Divide by 8 as they would have originally been scaled up, we now have UV space MVs
    // TODO: we could modify all the motion vectors generation shaders and remove the encoding and clamping, for higher quality. Also they are dithered in the distance, which isn't great.
#if 0 // Test: ignore distance threshold
    isDynamicObject = true;
#else // Threshold against its depth, given that they didn't have any depth test during dynamic MVs writing. The velocity buffer is cleared with 1 depth, so by default pixels are rejected.
#if 0 // Luma: disable random offsets that seemengly ended up accepting MVs for stuff that was beyond our depth (Luma upgrades the texture buffers quality, so maybe that was a workaround for 8bit UNORM). Disabled as this breaks motion vectors in the sky... Maybe we could add the offset based on the starting depth, but whatever.
    isDynamicObject = velocity.z <= saturate(invLinearDepth * 0.01); // Depth was also scaled by 0.01 when written in the velocity buffer. Saturate is necessary otherwise it'd catch the sky as well (or is it???).
#else
    isDynamicObject = velocity.z < saturate(invLinearDepth * 0.01 + 0.0058823498);
#endif
#endif

    // This seems to remove the (unwanted) influence of TAA camera jitters from the dynamic objects motion vectors (they are still a bit unstable but good enough!)
    dynamicObjectsMotionVectors.xy -= LumaData.GameData.CurrJitters * 0.5;

    finalMotionVectors = isDynamicObject ? dynamicObjectsMotionVectors : finalMotionVectors;
  }

  float2 size;
  DepthTexture.GetDimensions(size.x, size.y);
  float invAspectRatio = size.y / size.x;
  
  bool2 lowVelocityThreshold = abs(finalMotionVectors) < (0.0005 * float2(invAspectRatio, 1.0)); // Luma: this was not aspect ratio friendly and would have been ineffective in UW, so stretch the threshold accordingly horizontally
#if 0 // Luma: this threshold doesn't really seem necessary
  lowVelocityThreshold = 0.0;
#endif
  float2 finalSafeMotionVectors = lowVelocityThreshold ? 0.0 : finalMotionVectors;

  r1.xy = float2(8,8) * finalSafeMotionVectors; // Scale by 8 before encoding, again!
#if FIX_MOTION_BLUR_SHUTTER_SPEED // Luma: scale up motion blur to match the behaviour at 60fps
  r1.xy *= LumaData.CustomData3 / 60.0;
#endif
#if 0 // Luma: disable clamping, it doesn't seem necessary, if the texture is upgraded, we could easily allow bigger values, even if it'd result in more motion blur
  r1.xy = clamp(r1.xy, -1.0, 1.0); // Constrain to -1<->+1
#endif
  o0.xy = r1.xy * 0.5 + 0.5; // Encode to 0<->1
  o0.z = depth;
  o0.w = 0; // Unused

#if _A1037803
  // Luma: ignore velocity threshold ("finalSafeMotionVectors"), it would serve no purpose, and I don't understand why it was there, possibly because MVs seem to be jittered (acknowleding the diff between the prev and curr frame jitters), so we leave it on Motion Blur MVs
  o1.xy = finalMotionVectors;
#if 0 // Debug dynamic objects
  o1.xy = isDynamicObject;
#endif
#endif
}