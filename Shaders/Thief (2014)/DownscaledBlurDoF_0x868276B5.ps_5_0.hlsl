#include "../Includes/Math.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.xy = cb0[14].xy + v0.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r1.xyzw = cb0[14].xyxy * float4(1,-1,-1,1) + v0.xyxy;
  r2.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.xyzw = t0.Sample(s0_s, r1.zw).xyzw;
  r3.xyzw = r2.xyzw * r2.w;
  r2.x = r2.w + r0.w;
  r0.xyzw = r0.xyzw * r0.w + r3.xyzw;
  r2.yz = v0.xy - cb0[14].xy;
  r3.xyzw = t0.Sample(s0_s, r2.yz).xyzw;
  r0.xyzw += r3.xyzw * r3.w;
  r2.x += r3.w;
  r2.x += r1.w;
  r0.xyzw += r1.xyzw * r1.w;
  r1.xw = cb0[14].xy;
  r1.yz = float2(0,0);
  r3.xyzw = r1.xyzw * float4(2,2,2,2) + v0.xyxy;
  r1.xy = r1.xy * float2(-2,2) + v0.xy;
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r4.xyzw = t0.Sample(s0_s, r3.xy).xyzw;
  r3.xyzw = t0.Sample(s0_s, r3.zw).xyzw;
  r2.y = 0.75 * r4.w;
  r5.xyzw = float4(1,1,1,0.75) * r4.xyzw;
  r2.x += r4.w * 0.75;
  r2.x += r1.w * 0.75;
  r2.x += r3.w * 0.75;
  r0.xyzw += r5.xyzw * r2.y;
  r1.w = 0.75 * r1.w;
  r0.xyzw += r1.xyzw * r1.w;
  r3.w *= 0.75;
  r0.xyzw += r3.xyzw * r3.w;
  r1.x = 2;
  r1.y = cb0[14].y;
  r1.xy = float2(0,-2) * r1.xy + v0.xy;
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.w *= 0.75;
  r3.x = r1.w + r2.x;
  r0.xyzw += r1.xyzw * r1.w;
  o0.xyzw = r3.x != 0.f ? (r0.xyzw / r3.x) : float4(r1.xyz, 0.f);
}