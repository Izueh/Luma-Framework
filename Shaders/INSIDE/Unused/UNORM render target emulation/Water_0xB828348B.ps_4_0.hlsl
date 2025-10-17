Texture2D<float4> t2 : register(t2);
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
  float4 cb0[12];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float v1 : TEXCOORD0,
  float3 w1 : TEXCOORD2,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD3,
  nointerpolation float v4 : TEXCOORD4,
  nointerpolation float3 w4 : TEXCOORD5,
  nointerpolation float2 v5 : TEXCOORD6,
  float4 v6 : TEXCOORD7,
  float4 v7 : TEXCOORD8,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyzw = t2.Load(r0.xyz).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.x = -v1.x + r0.x;
  r0.x = saturate(v4.x * r0.x);
  r0.y = dot(v3.xyz, v3.xyz);
  r0.y = saturate(-0.75 + r0.y);
  r0.x = r0.x * r0.y;
  r0.yz = v6.xy / abs(v6.zw);
  r0.y = saturate(min(r0.y, r0.z));
  r0.z = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * r0.y;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, v2.zw).xyzw;
  r1.xyzw = r2.wxwx + r1.wxwx;
  r1.xyzw = r1.xyzw * float4(2,2,2,2) + float4(-2,-2,-2,-2);
  r2.xyzw = w4.xyzz * r1.xyzw;
  r0.y = dot(r2.zw, r2.zw);
  r3.y = 1 + -r0.y;
  r3.xz = r2.zw;
  r0.y = dot(r3.xyz, w1.xyz);
  r0.z = dot(w1.xyz, w1.xyz);
  r0.z = rsqrt(r0.z);
  r0.y = saturate(r0.y * -r0.z + 1);
  r0.y = saturate(r0.y * v5.x + v5.y);
  r0.z = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.y = r0.z * r0.y;
  r0.z = v7.w / v0.w;
  r0.z = saturate(8 * r0.z);
  r0.y = r0.y * r0.z + 1;
  r0.y = r0.y + -r0.z;
  r0.z = r0.y * r0.x;
  r1.zw = v7.xy / v7.zz;
  r1.zw = r1.zw * cb0[11].xy + cb0[11].zw;
  r1.xy = r1.xy * w4.xy + r1.zw;
  r3.xyzw = t1.SampleLevel(s1_s, r1.xy, 0).xyzw;
  r4.xyzw = t1.SampleLevel(s1_s, r1.zw, 0).xyzw;
  r0.w = min(r4.w, r3.w);
  r1.xy = r2.xy * r0.ww + r1.zw;
  r1.xyzw = t1.SampleLevel(s1_s, r1.xy, 0).xyzw;
  r1.xyz = r1.xyz * r0.zzz;
  r0.zw = v0.xy * cb1[6].zw + v0.ww;
  r0.zw = cb1[1].xx + r0.zw;
  r0.zw = float2(5.39870024,5.44210005) * r0.zw;
  r0.zw = frac(r0.zw);
  r2.xy = float2(21.5351009,14.3136997) + r0.zw;
  r2.x = dot(r0.wz, r2.xy);
  r0.zw = r2.xx + r0.zw;
  r0.z = r0.z * r0.w;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.zzzz;
  r3.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r0.zzzz;
  r3.xyzw = frac(r3.xyzw);
  r2.xyzw = frac(r2.xyzw);
  r2.xyzw = r2.xyzw + r3.xyzw;
  r2.xyzw = float4(-1,-1,-1,-1) + r2.xyzw;
  r0.z = saturate(v1.x * -cb0[10].x + 1);
  r0.w = r0.z * r0.z;
  r0.z = -r0.z * 2 + 3;
  r0.z = r0.w * r0.z;
  r0.w = cb0[9].w * r0.z;
  r0.z = -r0.z * cb0[9].w + 1;
  r0.y = r0.y * r0.z + r0.w;
  r1.w = r0.y * r0.x;
  o0.xyzw = -r2.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.100000001) + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}