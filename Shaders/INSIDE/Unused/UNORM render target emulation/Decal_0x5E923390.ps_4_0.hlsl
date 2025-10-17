Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

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
  float4 cb0[14];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  nointerpolation float3 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r1.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r0.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = dot(r0.xyz, v2.xyz);
  r0.x = saturate(-cb0[13].x + r0.x);
  r0.y = cb1[7].z * r1.x + cb1[7].w;
  r0.y = 1 / r0.y;
  r1.xyz = v1.xyz / v0.www;
  r2.x = r0.y * r1.x + cb2[8].w;
  r2.z = r0.y * r1.y + cb2[9].w;
  r2.y = r0.y * r1.z + cb2[10].w;
  r0.yzw = cmp(float3(0.5,0.5,0.5) < abs(r2.xzy));
  r1.xyz = float3(0.5,0.5,0.5) + r2.xyz;
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  if (r0.y != 0) discard;
  r2.xyzw = t0.SampleLevel(s0_s, r1.xy, 0).xyzw;
  r0.x = r2.w * r0.x;
  r0.yzw = cb0[12].xyz * r2.xyz;
  r1.w = 0.5;
  r1.xyzw = t1.SampleLevel(s1_s, r1.zw, 0).xyzw;
  r0.x = r1.x * r0.x;
  r1.x = saturate(v1.w);
  r0.x = r1.x * r0.x;
  r1.w = cb0[12].w * r0.x;
  r0.xyz = r1.www * r0.yzw;
  r1.xyz = r0.xyz + r0.xyz;
  r0.xy = v0.xy * cb1[6].zw + v0.zz;
  r0.xy = cb1[1].xx + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0 = saturate(o0);
}