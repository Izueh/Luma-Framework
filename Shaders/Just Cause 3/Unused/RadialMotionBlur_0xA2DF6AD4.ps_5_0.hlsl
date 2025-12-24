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

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = v1.xy * float2(2,2) + RadialBlurPosXY.xy;
  r0.xy = float2(-1,-1) + r0.xy;
  r1.x = AspectRatio;
  r1.y = 1;
  r0.xy = r0.xy / r1.xy;
  r0.z = dot(r0.xy, r0.xy);
  r0.w = sqrt(r0.z);
  r0.z = rsqrt(r0.z);
  r0.xy = r0.xy * r0.zz;
  r0.z = saturate(-RadialBlurOffset + r0.w);
  r0.z = r0.z * r0.z;
  r0.z = RadialBlurFactor * r0.z;
  r0.zw = -r0.xy * r0.zz;
  r1.z = dot(v1.xy, float2(398.48291,-277.2948));
  r1.z = frac(r1.z);
  r0.xy = r0.zw * r1.xy;
  r1.zw = r1.zz * r0.xy + v1.xy;
  r0.yz = r0.zw * r1.xy + r1.zw;
  r2.xyz = SceneTexture.Sample(LinearSampler_s, r0.yz).xyz;
  r3.xyz = SceneTexture.Sample(LinearSampler_s, r1.zw).xyz;
  r2.xyz = r3.xyz + r2.xyz;
  r0.yz = r0.xw * float2(2,2) + r1.zw;
  r3.xyz = SceneTexture.Sample(LinearSampler_s, r0.yz).xyz;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyzw = r0.xwxw * float4(3,3,4,4) + r1.zwzw;
  r0.xyzw = r0.xwxw * float4(5,5,6,6) + r1.zwzw;
  r1.xyz = SceneTexture.Sample(LinearSampler_s, r3.xy).xyz;
  r3.xyz = SceneTexture.Sample(LinearSampler_s, r3.zw).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r1.xyz = r1.xyz + r3.xyz;
  r2.xyz = SceneTexture.Sample(LinearSampler_s, r0.xy).xyz;
  r0.xyz = SceneTexture.Sample(LinearSampler_s, r0.zw).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  o0.xyz = float3(0.142857149,0.142857149,0.142857149) * r0.xyz;
  o0.w = 1;
}