#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 VignetteCentreXyScaleXy : packoffset(c0);
  float4 VignetteAngle : packoffset(c1);
  float4 BlurMatrixXXX : packoffset(c2);
  float4 BlurMatrixYYY : packoffset(c3);
  float4 BlurMatrixWWW : packoffset(c4);
}

void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float3 o2 : TEXCOORD1)
{
  float4 r0,r1;
  o0.xyz = v0.xyz;
  o0.w = 1;
  sincos(VignetteAngle.x, r0.x, r1.x);
  r0.yz = v1.yx * 2.0 - 1.0;
  r0.xw = r0.yz * r0.x;
  r1.z = r0.z * r1.x + r0.x;
  r1.w = r0.y * r1.x - r0.w;
  o1.zw = VignetteCentreXyScaleXy.zw * (-VignetteCentreXyScaleXy.xy * 2.0 + 1.0 + r1.zw);
  o1.xy = v1.xy;
  // These determine by how much the MB UVs are shifted by from each movement axis. Specifcally, they determine the central point of the motion blur.
  // XY are absolute offsets of MV (independent from the screen location), while Z determines how much the MV offset grows as we get further from the center of the screen.
  // It's kind of impossible to tweak them individually because they are all generated from a single different source value and if any individual value is changed, the result is heavily offsetted.
  o2.xyz = BlurMatrixXXX.xyz * v1.x + BlurMatrixYYY.xyz * v1.y + BlurMatrixWWW.xyz; // These are not scaled by aspect ratio at all (and probably they shouldn't, due to fancy math) (resolution shouldn't matter as it's all uv space)
}