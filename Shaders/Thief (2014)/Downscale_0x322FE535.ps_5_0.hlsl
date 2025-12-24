#include "../Includes/Math.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float2 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r1.x = saturate(r0.w * 2.0);
  r1.w = 1.0 - r1.x;
  r1.xyz = r0.xyz;
  r2.xyzw = r1.xyzw * r1.w;
  r3.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
  r0.x = saturate(r3.w * 2.0);
  r4.w = 1.0 - r0.x;
  r4.xyz = r3.xyz;
  r2.xyzw += r4.xyzw * r4.w;
  r0.x = r4.w + r1.w;
  r5.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.y = saturate(r5.w * 2.0);
  r6.w = 1.0 - r0.y;
  r6.xyz = r5.xyz;
  r2.xyzw += r6.xyzw * r6.w;
  r0.x += r6.w;
  r7.xyzw = t0.Sample(s0_s, v3.xy).xyzw;
  r0.y = saturate(r7.w * 2.0);
  r8.w = 1.0 - r0.y;
  r8.xyz = r7.xyz;
  r2.xyzw += r8.xyzw * r8.w;
  r0.x += r8.w;
  o0.xyzw = r0.x != 0.f ? (r2.xyzw / r0.x) : float4(r8.xyz, 0.f);
  r0.x = r3.w - 0.5;
  r1.xyz += r4.xyz;
  r0.y = r0.w - 0.5;
  r1.xyz += r6.xyz;
  r0.z = r5.w - 0.5;
  r1.xyz += r8.xyz;
  r0.w = r7.w - 0.5;
  r0.xyzw = saturate(r0.xyzw * 2.0);
  o1.xyz = r1.xyz / 4.f;
  r0.xz = max(r0.xz, r0.yw);
  o1.w = max(r0.x, r0.z);
}