#include "Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xy = cb0[13].ww * float2(1259,1277) + v0.xy;
  r0.xy = cb0[14].zw * r0.xy;
  r0.xyzw = t0.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r1.xy = float2(6.28318548,0.25) * r0.xy;
  sincos(r1.x, r0.x, r2.x);
  r2.y = r0.x;
  r2.xy = 3.1 * r2.xy;

  int r0ix = asint(r1.y) >> 1;
  r0ix += 0x1fbd1df5;
  r0.x = asfloat(r0ix);

  r1.xyz = r0.y * 0.25 + float3(0.25, 0.5, 0.75);

  int3 r1ixyz = asint(r1.xyz) >> 1;
  r1ixyz += 0x1fbd1df5;
  r1.xyz = asfloat(r1ixyz);

  r3.xy = r2.xy * r0.x;
  r2.zw = -r2.yx;
  r3.zw = r2.zx * r1.x;
  r3.xyzw = v0.xyxy * 0.5 + r3.xyzw;
  r4.xyzw = t1.Load(int3(r3.zw, 0)).xyzw;
  r3.xyzw = t1.Load(int3(r3.xy, 0)).xyzw;
  r5.xy = -r2.xy * r1.y;
  r5.zw = r2.yw * r1.z;
  r1.xyzw = v0.xyxy * 0.5 + r5.xyzw;
  r2.xyzw = t1.Load(int3(r1.zw, 0)).xyzw;
  r1.xyzw = t1.Load(int3(r1.xy, 0)).xyzw;
  r5.x = dot(r4.yzw, float3(1, 0.00392156886, 1.53787005e-005));
  r5.y = dot(r3.yzw, float3(1, 0.00392156886, 1.53787005e-005));
  r5.z = dot(r2.yzw, float3(1, 0.00392156886, 1.53787005e-005));
  r5.w = dot(r1.yzw, float3(1, 0.00392156886, 1.53787005e-005));
  r0.x = min(r5.z, r5.w);
  r0.x = min(r5.y, r0.x);
  r0.x = min(r5.x, r0.x);
  r1.y = max(r5.z, r5.w);
  r1.y = max(r5.y, r1.y);
  r1.y = max(r5.x, r1.y);
  r0.x = r1.y - r0.x;
  r1.y = r5.x + r5.y;
  r1.y = r1.y + r5.z;
  r1.y = r1.y + r5.w;
  r1.y = 0.25 * r1.y;
  r0.x = r0.x / r1.y;
  r0.x = (r0.x < 0.1);
  if (r0.x != 0) {
    r0.x = r3.x * r3.x;
    r0.x = r4.x * r4.x + r0.x;
    r0.x = r2.x * r2.x + r0.x;
    r0.x = r1.x * r1.x + r0.x;
    r0.x = 0.25 * r0.x;
  } else {
    r6.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
    r1.y = cb1[7].x * r6.x + cb1[7].y;
    r1.y = 1 / r1.y;
    r5.xyzw = r5.xyzw + -r1.yyyy;
    r5.xyzw = float4(9.99999975e-006,9.99999975e-006,9.99999975e-006,9.99999975e-006) + abs(r5.xyzw);
    r5.xyzw = float4(1,1,1,1) / r5.xyzw;
    r1.y = r5.x + r5.y;
    r1.y = r1.y + r5.z;
    r1.y = r1.y + r5.w;
    r4.y = r3.x;
    r4.z = r2.x;
    r4.w = r1.x;
    r2.xyzw = r4.xyzw * r4.xyzw;
    r1.x = dot(r5.xyzw, r2.xyzw);
    r0.x = r1.x / r1.y;
  }
  r0.yzw = float3(0.00392156886, 0.00392156886, 0.00392156886) * r0.yzw;
#if !ENABLE_DITHER
  r0.yzw = 0.0;
#endif
  o0.xyz = r0.x * cb0[12].xyz - r0.yzw;
  o0.w = 1;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}