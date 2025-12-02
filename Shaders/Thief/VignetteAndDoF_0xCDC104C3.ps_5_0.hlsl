#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[6];
}

// Vignette, DoF, and maybe more
void main(
  float4 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = dot(v0.zw, v0.zw);
  r0.x = sqrt(r0.x);
  r0.yz = v0.zw / r0.xx;
  r0.x = -cb0[5].x + r0.x;
  r0.x = cb0[5].y * r0.x;
  r0.x = cb0[4].x * r0.x;
  r1.xy = r0.yz * cb0[4].zw + v0.xy;
  r1.xyz = t0.Sample(s0_s, r1.xy).xyz;
  r2.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r3.xy = cb0[4].zw * r0.yz;
  r0.yz = -r0.yz * cb0[4].zw + v0.xy;
  r0.yzw = t0.Sample(s0_s, r0.yz).xyz;
  r4.xyzw = r3.xyxy * float4(1.5,1.5,2.5,2.5) + v0.xyxy;
  r5.xyz = t0.Sample(s0_s, r4.xy).xyz;
  r4.xyz = t0.Sample(s0_s, r4.zw).xyz;
  r1.xyz = r5.xyz + r1.xyz;
  r3.zw = r3.xy * float2(2,2) + v0.xy;
  r5.xyz = t0.Sample(s0_s, r3.zw).xyz;
  r1.xyz = r5.xyz + r1.xyz;
  r1.xyz = r1.xyz + r4.xyz;
  r3.zw = r3.xy * float2(3,3) + v0.xy;
  r4.xyz = t0.Sample(s0_s, r3.zw).xyz;
  r1.xyz = r4.xyz + r1.xyz;
  r0.yzw = r1.xyz + r0.yzw;
  r1.xyzw = -r3.xyxy * float4(1.5,1.5,2.5,2.5) + v0.xyxy;
  r4.xyz = t0.Sample(s0_s, r1.xy).xyz;
  r1.xyz = t0.Sample(s0_s, r1.zw).xyz;
  r0.yzw = r4.xyz + r0.yzw;
  r3.zw = -r3.xy * float2(2,2) + v0.xy;
  r3.xy = -r3.xy * float2(3,3) + v0.xy;
  r4.xyz = t0.Sample(s0_s, r3.xy).xyz;
  r3.xyz = t0.Sample(s0_s, r3.zw).xyz;
  r0.yzw = r3.xyz + r0.yzw;
  r0.yzw = r0.yzw + r1.xyz;
  r0.yzw = r0.yzw + r4.xyz;
  r0.yzw = r0.yzw / 11.f + -r2.xyz;
  r1.x = saturate(r0.x);
  r0.x = saturate(cb0[4].y * r0.x);
  r0.yzw = r1.xxx * r0.yzw + r2.xyz;
  r1.x = dot(r0.yzw, 1.0 / 3.0);
  r1.xyz = r1.xxx + -r0.yzw;
  r2.xyzw = -cb0[3].xyzw + cb0[2].xyzw;
  r2.xyzw = r0.x * r2.xyzw + cb0[3].xyzw;
  r0.xyz = r2.www * r1.xyz + r0.yzw;
  o0.xyz = r0.xyz * r2.xyz;
  o0.w = 1; // Irrelevant
}