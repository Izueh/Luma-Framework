// ---- Created with 3Dmigoto v1.3.16 on Mon Dec 30 15:51:13 2024
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[7];
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

  r0.xy = cb1[2].xy + v1.xy;
  r0.xy = max(cb1[0].xy, r0.xy);
  r0.xy = min(cb1[0].zw, r0.xy);
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyzw = cb1[2].zzzz * r0.xyzw;
  r1.xy = cb1[1].xy + v1.xy;
  r1.xy = max(cb1[0].xy, r1.xy);
  r1.xy = min(cb1[0].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r0.xyzw = r1.xyzw * cb1[1].zzzz + r0.xyzw;
  r1.xy = cb1[3].xy + v1.xy;
  r1.xy = max(cb1[0].xy, r1.xy);
  r1.xy = min(cb1[0].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r0.xyzw = r1.xyzw * cb1[3].zzzz + r0.xyzw;
  r1.xy = cb1[4].xy + v1.xy;
  r1.xy = max(cb1[0].xy, r1.xy);
  r1.xy = min(cb1[0].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r0.xyzw = r1.xyzw * cb1[4].zzzz + r0.xyzw;
  r1.xy = cb1[5].xy + v1.xy;
  r1.xy = max(cb1[0].xy, r1.xy);
  r1.xy = min(cb1[0].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r0.xyzw = r1.xyzw * cb1[5].zzzz + r0.xyzw;
  r1.xy = cb1[6].xy + v1.xy;
  r1.xy = max(cb1[0].xy, r1.xy);
  r1.xy = min(cb1[0].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  o0.xyzw = r1.xyzw * cb1[6].zzzz + r0.xyzw;
  return;
}