Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[1];
}

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
  float4 cb0[14];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  nointerpolation float3 v2 : TEXCOORD2,
  nointerpolation float2 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r1.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r1.x = cb1[7].z * r1.x + cb1[7].w;
  r1.x = 1 / r1.x;
  r1.yzw = v1.xyz / v0.w;
  r2.x = r1.x * r1.y + cb2[8].w;
  r2.y = r1.x * r1.z + cb2[9].w;
  r2.z = r1.x * r1.w + cb2[10].w;
  r1.x = r1.x * v3.x + v3.y;
  r1.x = max(0, r1.x);
  r1.x = min(cb3[0].w, r1.x);
  r1.yzw = cmp(float3(0.5,0.5,0.5) < abs(r2.xyz));
  r2.xyz = float3(0.5,0.5,0.5) + r2.xyz;
  r1.y = asfloat(asint(r1.z) | asint(r1.y));
  r1.y = asfloat(asint(r1.w) | asint(r1.y));
  if (r1.y != 0) discard;
  r3.xyzw = t4.Load(int3(v0.xy, 0)).xyzw;
  r0.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = dot(r0.xyz, v2.xyz);
  r0.x = r0.x * cb0[12].y + 1;
  r0.yzw = max(float3(0.000977517106,0.000977517106,0.000977517106), r3.xyz);
  r0.yzw = log2(r0.yzw);
  r1.y = 1 + -cb0[12].z;
  r0.yzw = -r0.yzw * cb0[12].zzz + r1.yyy;
  r3.xyzw = t0.SampleLevel(s0_s, r2.xz, 0).xyzw;
  r3.xyz = r3.xyz * r0.yzw;
  r4.xyzw = t1.SampleLevel(s1_s, r2.xz, 0).xyzw;
  r0.y = 1 + -r2.y;
  r0.y = r0.y * cb0[12].x + 1;
  r0.xy = -cb0[12].yx + r0.xy;
  r3.w = r4.w;
  r2.xyzw = cb0[13].xyzw * r3.xyzw;
  r1.yzw = -r3.xyz * cb0[13].xyz + cb3[0].xyz;
  r1.xyz = r1.xxx * r1.yzw + r2.xyz;
  r0.y = r2.w * r0.y;
  r0.w = saturate(dot(r0.yy, r0.xx));
  r0.xyz = r1.xyz * r0.www;
  r1.xy = v0.xy * cb1[6].zw + v0.zz;
  r1.xy = cb1[1].xx + r1.xy;
  r1.zw = float2(0.622099996,0.622099996) + r1.xy;
  r1.xyzw = float4(5.39870024,5.44210005,5.39870024,5.44210005) * r1.xyzw;
  r1.xyzw = frac(r1.xyzw);
  r2.xy = float2(21.5351009,14.3136997) + r1.zw;
  r2.x = dot(r1.wz, r2.xy);
  r1.zw = r2.xx + r1.zw;
  r1.z = r1.z * r1.w;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.zzzz;
  r2.xyzw = frac(r2.xyzw);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r1.xyzw = r1.xyzw + r2.xyzw;
  o0.xyzw = -r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.0666666701) + r0.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}