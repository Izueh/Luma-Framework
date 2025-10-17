#include "Includes/Common.hlsl"

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2); // Scene
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[22];
}

// Also (or only) used to draw water surface moving effects (e.g. waves/ripples)
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  nointerpolation float4 v3 : TEXCOORD2,
  float v4 : TEXCOORD3,
  float3 w4 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.y = -v1.x + r0.x;
  r0.y = saturate(r0.y / cb0[7].w);
  r0.y = v3.x * r0.y;
  r0.z = saturate(-cb1[5].y + v1.x);
  r0.y = r0.y * r0.z;
  r0.z = dot(w4.xyz, w4.xyz);
  r0.z = saturate(-0.75 + r0.z);
  r0.y = r0.y * r0.z;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, v2.zw).xyzw;
  r0.zw = r2.wx + r1.wx;
  r0.zw = float2(-1,-1) + r0.zw;
  r0.yz = r0.zw * r0.yy;
  r0.w = dot(r0.yz, r0.yz);
  r1.x = v4.x / v0.w;
  r0.x = r0.x * r1.x + cb1[4].z;
  r0.x = -1 + r0.x;
  r0.x = min(v1.w, r0.x);
  r0.x = saturate(r0.x / cb0[7].w);
  r0.x = cb0[7].z * r0.x;
  r0.x = 0.00200000009 * r0.x;
#if _31DECB17 // This is part 1
  r0.x *= saturate(1 + cb1[5].y - v1.x);
#endif
  r1.y = r0.x * r0.x;
  r0.w = (r0.w < r1.y);
  r1.yz = v0.xy * cb0[11].xy + cb0[21].ww;
  r2.xyzw = t1.SampleLevel(s1_s, r1.yz, 0).xyzw;
  r1.yz = float2(5.39870024,5.44210005) * r1.yz;
  r1.yz = frac(r1.yz);
  r1.w = sqrt(r2.y);
  r0.x = r1.w * r0.x;
  r0.yz *= r2.y * 0.5 + 0.5;
  sincos(6.28000021 * r2.x, r2.x, r3.x);
  r2.y = r3.x;
  r2.xy = r2.xy * r0.xx;
  r0.xy = r0.ww ? r2.xy : r0.yz;
  r0.xy = cb1[6].yy * r0.xy;
  r0.xy = r0.xy * float2(1.33333337,1.33333337) + v0.xy;
  r2.xyzw = t3.Load(int3(r0.xy, 0)).xyzw;
  r0.z = cb1[7].z * r2.x + cb1[7].w;
  r0.z = 1 / r0.z;
  r0.z = r0.z * r1.x + cb1[4].z;
  r0.z = r0.w ? r0.z : r2.x;
  r1.x = v1.y / v1.z;
  r0.w = r0.w ? 1 : r1.x;
  r0.z = (r0.w < r0.z);
  r0.xy = r0.zz ? r0.xy : v0.xy;
  r0.xyzw = t2.Load(int3(r0.xy, 0)).xyzw;
  r0.xyzw = IsNaN_Strict(r0.xyzw) ? 0.0 : r0.xyzw; // Luma: prevent NaNs from spreading
  r1.xw = float2(21.5351009,14.3136997) + r1.yz;
  r1.x = dot(r1.zy, r1.xw);
  r1.xy = r1.yz + r1.xx;
  r1.x = r1.x * r1.y;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r2.xyzw = frac(r2.xyzw);
  r1.xyzw = r2.xyzw + r1.xyzw;
  r1.xyzw = float4(-1,-1,-1,-1) + r1.xyzw;
#if ENABLE_DITHER
  o0.xyzw = r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r0.xyzw;
#else
  o0.xyzw = r0.xyzw;
#endif
}