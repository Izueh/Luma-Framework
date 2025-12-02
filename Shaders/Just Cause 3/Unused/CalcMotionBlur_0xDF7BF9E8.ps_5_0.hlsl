cbuffer MotionBlurFrameConsts : register(b3)
{
  row_major float4x4 PrevViewProjMatrix : packoffset(c0);
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

SamplerState LinearSampler_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);
Texture2D<float4> VelocityTexture : register(t2);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = VelocityTexture.Sample(LinearSampler_s, v1.xy).xy;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.zw = (float2(0,0) < r0.xy);
  r1.xy = saturate(r0.xy * float2(0.125,0.125) + float2(-0.00787401572,-0.00787401572));
  r0.xy = saturate(-r0.xy * float2(0.125,0.125) + float2(-0.00787401572,-0.00787401572));
  r0.xy = r0.zw ? r1.xy : -r0.xy;
  r0.xy = ExposureTime * r0.xy;
  r0.z = dot(r0.xy, r0.xy);
  r1.xyz = SceneTexture.Sample(LinearSampler_s, v1.xy).xyz;
  if (9.99999994e-009 < r0.z) {
    r0.z = rsqrt(r0.z);
    r0.z = saturate(MaxBlurRadius * r0.z);
    r2.xy = r0.xy * r0.zz;
    r3.xyzw = r2.xyxy * float4(-3,-3,-2,-2) + v1.xyxy;
    r4.xyz = SceneTexture.SampleLevel(LinearSampler_s, r3.xy, 0).xyz;
    r4.xyz = r4.xyz + r1.xyz;
    r3.xyz = SceneTexture.SampleLevel(LinearSampler_s, r3.zw, 0).xyz;
    r3.xyz = r4.xyz + r3.xyz;
    r2.zw = -r0.xy * r0.zz + v1.xy;
    r4.xyz = SceneTexture.SampleLevel(LinearSampler_s, r2.zw, 0).xyz;
    r3.xyz = r4.xyz + r3.xyz;
    r0.xy = r0.xy * r0.zz + v1.xy;
    r0.xyz = SceneTexture.SampleLevel(LinearSampler_s, r0.xy, 0).xyz;
    r0.xyz = r3.xyz + r0.xyz;
    r2.zw = r2.xy * float2(2,2) + v1.xy;
    r3.xyz = SceneTexture.SampleLevel(LinearSampler_s, r2.zw, 0).xyz;
    r0.xyz = r3.xyz + r0.xyz;
    r2.xy = r2.xy * float2(3,3) + v1.xy;
    r2.xyz = SceneTexture.SampleLevel(LinearSampler_s, r2.xy, 0).xyz;
    r0.xyz = r2.xyz + r0.xyz;
    r1.xyz = float3(0.142857149,0.142857149,0.142857149) * r0.xyz;
  }
  o0.xyz = r1.xyz;
  o0.w = 1;
}