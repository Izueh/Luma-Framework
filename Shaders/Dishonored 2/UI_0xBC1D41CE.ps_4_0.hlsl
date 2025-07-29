// ---- Created with 3Dmigoto v1.3.16 on Mon Dec 30 15:50:29 2024
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[5];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r1.x = dot(cb1[0].xyzw, r0.xyzw);
  r1.x = cb1[4].x * r0.w + r1.x;
  r1.w = dot(cb1[1].xyzw, r0.xyzw);
  r1.y = cb1[4].y * r0.w + r1.w;
  r0.x = dot(cb1[2].xyzw, r0.xyzw);
  r1.z = cb1[4].z * r0.w + r0.x;
  o0.w = cb1[3].w * r0.w;
  o0.xyz = cb1[3].www * r1.xyz;
  return;
}