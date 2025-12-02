cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

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

SamplerState PointSampler_s : register(s1);
Texture2D<float4> VelocityTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  float depth = DepthTexture.Load(int3(v0.xy, 0)).x;
  r0.y = depth * LinearDepthParams.x + LinearDepthParams.y;
  r0.y = 1 / r0.y;
  r1.xyz = r0.y * v2.xyz + Globals[4].xyz;
  r2.xyz = PrevViewProjMatrix._m10_m11_m13 * r1.y;
  r1.xyw = r1.x * PrevViewProjMatrix._m00_m01_m03 + r2.xyz;
  r1.xyz = r1.z * PrevViewProjMatrix._m20_m21_m23 + r1.xyw;
  r1.xyz = PrevViewProjMatrix._m30_m31_m33 + r1.xyz;
  r0.zw = r1.xy / r1.z;
  r0.zw = r0.zw * float2(0.5,-0.5) + float2(0.5,0.5);
  r0.zw = v1.xy + -r0.zw;
  if (CameraMotionBlurOnly == 0) {
    r1.xyz = VelocityTexture.Sample(PointSampler_s, v1.xy).xyz; // Note: if this texture is upgraded, z can have a value beyond 1, which seems fine anyway
    r1.xy = r1.xy * 2.0 - 1.0;
    r1.xy = float2(0.125,0.125) * r1.xy;
    r0.y = saturate(r0.y * 0.01 + 0.0058823498);
    r0.y = (r1.z < r0.y);
    r0.zw = r0.y ? r1.xy : r0.zw;
  }
  r0.yz = float2(8,8) * r0.zw;
  r0.yz = clamp(r0.yz, -1.0, 1.0); // Constrain to -1<->+1
  o0.xy = r0.yz * 0.5 + 0.5; // To 0<->1
  o0.z = depth;
  o0.w = 0;
}