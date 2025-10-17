Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = t0.Sample(s2_s, v3.xy).xyzw;
  r1.xyzw = t0.Sample(s2_s, v3.zw).xyzw;
  r0.xy = r1.wx + r0.wx;
  r0.xy = r0.xy * float2(2,2) + float2(-2,-2);
  r0.xy = r0.xy * cb0[14].yy + v2.xy;
  r0.xyzw = t1.Sample(s1_s, r0.xy).xyzw;
  r0.x = -v1.w + r0.w;
  r0.y = 1 / cb0[11].x;
  r0.x = saturate(r0.x * r0.y);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.y = r0.x * cb0[10].w + -0.00999999978;
  r0.x = cb0[10].w * r0.x;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.y = dot(v2.zw, v2.zw);
  r0.y = sqrt(r0.y);
  r0.y = -1.10000002 + r0.y;
  r0.y = saturate(9.99999809 * r0.y);
  r0.z = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * r0.y;
  r0.yz = v4.xy / v4.ww;
  r1.xyzw = t2.SampleLevel(s0_s, r0.yz, 0).xyzw;
  r0.y = cb1[5].z * r1.x + -v4.z;
  r0.y = saturate(cb0[14].z * r0.y);
  r0.w = r0.x * r0.y;
  r1.xy = cb1[1].xx + v4.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r0.xyz = cb0[10].xyz * v1.xyz;
  o0.xyzw = -r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r0.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}