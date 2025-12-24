cbuffer _Globals : register(b0)
{
  float4 VignetteCentreXyScaleXy : packoffset(c0);
  float4 VignetteAngle : packoffset(c1);
}

void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0)
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
}