Buffer<float4> t4 : register(t4);

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
Texture2D<float4> SceneTexture : register(t0);
Texture2D<float4> VelocityTexture : register(t2);
Texture2D<float4> NeighborMaxTexture : register(t3);

#define cmp

// TODO: fix these, rename them and figure out how they work and if there's any missing... (seems so). This is Motion Blur or something else though?
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask;

  r0.x = HalfPixelLength + HalfPixelLength;
  r0.yz = UVScale.xy * v1.xy;
  r1.xyzw = SceneTexture.Sample(PointSampler_s, v1.xy).xyzw;
  r2.xy = NeighborMaxTexture.SampleLevel(PointSampler_s, r0.yz, 0).xy;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r2.xy = float2(0.125,0.125) * r2.xy;
  r0.w = dot(r2.xy, r2.xy);
  r0.w = sqrt(r0.w);
  r2.z = ExposureTime * r0.w;
  r2.z = 0.5 * r2.z;
  r2.w = max(HalfPixelLength, r2.z);
  r2.w = min(MaxBlurRadiusLength, r2.w);
  r0.x = cmp(r0.x >= r2.w);
  if (r0.x != 0) {
    o0.xyzw = r1.xyzw;
    return;
  }
  r0.x = 10 * HalfPixelLength;
  r2.z = cmp(r2.z >= EpsilonRadiusLength);
  r0.w = r2.w / r0.w;
  r3.xy = r2.xy * r0.ww;
  r2.xy = r2.zz ? r3.xy : r2.xy;
  r0.yzw = VelocityTexture.SampleLevel(PointSampler_s, r0.yz, 0).zxy;
  r3.xy = r0.zw * float2(2,2) + float2(-1,-1);
  r3.xy = float2(0.125,0.125) * r3.xy;
  r0.w = dot(r3.xy, r3.xy);
  r0.w = sqrt(r0.w);
  r2.z = ExposureTime * r0.w;
  r2.z = 0.5 * r2.z;
  r3.z = cmp(r2.z >= EpsilonRadiusLength);
  r2.z = max(HalfPixelLength, r2.z);
  r2.z = min(MaxBlurRadiusLength, r2.z);
  r0.w = r2.z / r0.w;
  r4.xy = r3.xy * r0.ww;
  r3.xy = r3.zz ? r4.xy : r3.xy;
  r0.x = cmp(r2.z >= r0.x);
  r0.xw = r0.xx ? r3.xy : r2.xy;
  r2.x = dot(r0.xw, r0.xw);
  r2.x = rsqrt(r2.x);
  r0.xw = r2.xx * r0.xw;
  r2.x = cmp(r2.z < VarianceThresholdLength);
  r2.y = dot(r3.xy, r3.xy);
  r2.y = rsqrt(r2.y);
  r3.xy = r3.xy * r2.yy;
  r2.xy = r2.xx ? r0.xw : r3.xy;
  r3.xy = (int2)v0.xy;
  bitmask.y = ((~(-1 << 5)) << 5) & 0xffffffff;  r3.y = (((uint)r3.y << 5) & bitmask.y) | ((uint)0 & ~bitmask.y);
  bitmask.x = ((~(-1 << 5)) << 0) & 0xffffffff;  r3.x = (((uint)r3.x << 0) & bitmask.x) | ((uint)r3.y & ~bitmask.x);
  r3.x = t4.Load(r3.x).x;
  r3.y = 1 / r2.z;
  r3.y = CenterSampleWeight * r3.y;
  r1.xyz = r3.yyy * r1.xyz;
  r3.z = SampleCount;
  r3.w = 0.5 * r3.z;
  r3.xz = float2(-0.5,1) + r3.xz;
  r4.yw = rcp(r2.zz);
  r5.xyz = r1.xyz;
  r2.z = r3.y;
  r5.w = 0;
  while (true) {
    r6.x = cmp((int)r5.w >= SampleCount);
    if (r6.x != 0) break;
    r6.x = (int)r5.w;
    r6.x = cmp(r6.x == r3.w);
    if (r6.x != 0) {
      r5.w = (int)r5.w + 1;
      continue;
    }
    r6.x = (int)r5.w;
    r6.x = r6.x + r3.x;
    r6.x = 1 + r6.x;
    r6.x = r6.x / r3.z;
    r6.x = r6.x * 2 + -1;
    r6.x = 1.20000005 * r6.x;
    r6.x = max(-1, r6.x);
    r6.x = min(1, r6.x);
    r6.x = r6.x * r2.w;
    r6.y = (int)r5.w & 1;
    r6.y = cmp((int)r6.y == 1);
    r6.yz = r6.yy ? r2.xy : r0.xw;
    r6.yz = r6.xx * r6.yz + v1.xy;
    r6.yz = saturate(HalfPixelSize.xy + r6.yz);
    r7.xy = UVScale.xy * r6.yz;
    r7.xyz = VelocityTexture.SampleLevel(PointSampler_s, r7.xy, 0).xyz;
    r6.yzw = SceneTexture.SampleLevel(PointSampler_s, r6.yz, 0).xyz;
    r7.xy = r7.xy * float2(2,2) + float2(-1,-1);
    r7.xy = float2(0.125,0.125) * r7.xy;
    r7.x = dot(r7.xy, r7.xy);
    r7.x = sqrt(r7.x);
    r7.x = ExposureTime * r7.x;
    r7.x = 0.5 * r7.x;
    r7.x = max(HalfPixelLength, r7.x);
    r7.x = min(MaxBlurRadiusLength, r7.x);
    r0.z = r7.z;
    r7.yz = r0.zy * float2(50,50) + float2(1,1);
    r7.yz = saturate(-r0.yz * float2(50,50) + r7.yz);
    r4.xz = rcp(r7.xx);
    r8.xyzw = saturate(-abs(r6.xxxx) * r4.xyzw + float4(1,1,1.95000005,1.95000005));
    r0.z = dot(r7.yz, r8.xy);
    r4.x = dot(r8.zz, r8.ww);
    r0.z = r4.x + r0.z;
    r2.z = r2.z + r0.z;
    r5.xyz = r0.zzz * r6.yzw + r5.xyz;
    r5.w = (int)r5.w + 1;
  }
  o0.xyz = r5.xyz / r2.zzz;
  o0.w = r1.w;
}