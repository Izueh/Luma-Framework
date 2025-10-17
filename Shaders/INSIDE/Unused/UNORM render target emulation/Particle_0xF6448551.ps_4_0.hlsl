Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[20];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  nointerpolation float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = t0.SampleBias(s1_s, v1.zw, cb0[19].w).xyzw;
  r0.xy = r0.xy * cb0[19].yy + v1.xy;
  r0.z = v3.y * r0.z;
  r1.xyzw = t1.Sample(s0_s, r0.xy).xyzw;
  r0.x = saturate(v2.w);
  r0.x = r1.w * r0.x;
  r0.x = r0.x * v3.x + -r0.z;
  r0.y = -0.0500000007 + r0.x;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.yz = float2(0.5,0.5) * v0.xy;
  r1.xyzw = t2.Load(int3(r0.yz, 0)).xyzw;
  r0.y = cb1[5].z * r1.x;
  r0.y = saturate(r0.y * cb0[12].x + -v3.z);
  r0.x = r0.x * r0.y;
  r1.xyz = v2.xyz * r0.xxx;
  r1.w = v3.w * r0.x;
  r0.xy = v0.xy * float2(0.5,0.5) + v3.zz;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.0666666701) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}