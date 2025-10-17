Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[6];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = cb0[1].xx + v2.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xy = v2.xy / v2.ww;
  r1.xyzw = t1.SampleLevel(s0_s, r1.xy, 0).xyzw;
  r1.x = cb0[5].z * r1.x + -v2.z;
  r1.x = saturate(20 * r1.x);
  r2.xyzw = t0.Sample(s1_s, v3.xy).xyzw;
  r1.w = r2.w * r1.x;
  r1.xyz = v1.xyz;
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}