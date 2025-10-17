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
  float4 cb0[19];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v1.xy / v1.ww;
  r0.xyzw = t1.Sample(s0_s, r0.xy).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.x = -v1.z + r0.x;
  r0.x = saturate(r0.x * v2.z + v2.w);
  r1.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r0.x = r1.w * r0.x;
  r0.w = cb0[17].w * r0.x;
  r0.xyz = cb0[17].xyz * r0.www;
  r1.xy = cb1[1].xx + v1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  r2.w = r1.w / cb0[18].z;
  o0.xyzw = -r2.xyzw + r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}