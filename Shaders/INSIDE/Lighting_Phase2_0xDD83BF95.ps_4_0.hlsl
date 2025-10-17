#include "Includes/Common.hlsl"

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[11];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[8];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  nointerpolation float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  nointerpolation float3 v4 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v1.xy / v1.zz;
  r1.xyzw = t0.SampleLevel(s2_s, r0.xy, 0).xyzw;
  r0.xyzw = t1.SampleLevel(s3_s, r0.xy, 0).xyzw;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = saturate(dot(r0.xyz, v2.xyz));
  r0.x = r0.x * cb0[7].x + 1;
  r0.x = -cb0[7].x + r0.x;
  r0.y = cb1[7].z * r1.x + cb1[7].w;
  r0.y = 1 / r0.y;
  r0.y = r0.y / v1.z;
  r1.x = r0.y * v3.x + cb2[8].w;
  r1.y = r0.y * v3.y + cb2[9].w;
  r1.z = r0.y * v3.z + cb2[10].w;
  r0.yzw = cmp(float3(0.5,0.5,0.5) < abs(r1.xyz));
  r1.xyz = float3(0.5,0.5,0.5) + r1.xyz;
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  if (r0.y != 0) discard;
  r1.w = 0.5;
  r2.xyzw = t2.SampleLevel(s1_s, r1.yw, 0).xyzw;
  r1.xyzw = t3.SampleLevel(s0_s, r1.xz, 0).xyzw;
  r0.y = r1.w * r2.x;
  r0.x = r0.y * r0.x;
  r0.yz = cb1[1].xx + v1.xy;
  r0.yz = float2(5.39870024,5.44210005) * r0.yz;
  r0.yz = frac(r0.yz);
  r1.xy = float2(21.5351009,14.3136997) + r0.yz;
  r0.w = dot(r0.zy, r1.xy);
  r0.yz = r0.yz + r0.ww;
  r0.y = r0.y * r0.z;
  r0.yzw = float3(95.4307022,97.5901031,93.8368988) * r0.yyy;
  r0.yzw = frac(r0.yzw);
  r0.yzw = cb0[7].yyy * r0.yzw;
#if !ENABLE_DITHER
  r0.yzw = 0.0;
#endif
  o0.xyz = v4.xyz * r0.x - r0.yzw;
  o0.w = 1;
  
  // Luma: typical UNORM like clamping, needed due to the weird blending types this uses, that end up creating steps in lightings.
  // Note that this shader would already be live patched, but we add the dither branch to this as we can't patch dithering out easily otherwise.
  // Lighting might have been able to go negative as it's flipped log space, representing even brighter values,
  // but the peak doesn't seem to ever be reached, and there's enough range for HDR anyway.
  // If we wanted, we could detect the lighting shaders by the lack of the "75.0490875" pattern for their dithering, and possibly this pattern "cb1[7].z * r?.x + cb1[7].w"
  o0.rgb = max(o0.rgb, 0.0);
}