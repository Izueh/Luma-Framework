Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float w3 : TEXCOORD4,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = cb1[1].xx + v4.xy;
  r0.xy = float2(0.987653971,0.987653971) + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xy = v4.xy / v4.ww;
  r1.xyzw = t3.SampleLevel(s3_s, r1.xy, 0).xyzw;
  r1.x = cb1[5].z * r1.x + -v4.z;
  r1.x = saturate(cb0[7].x * r1.x);
  r2.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r3.xyzw = t1.Sample(s2_s, v2.zw).xyzw;
  r2.xyzw = r3.xyzw * r2.xyzw;
  r2.xyzw = cb0[11].yyyy * r2.xyzw;
  r2.xyzw = r2.xyzw * cb0[6].xyzw + -v1.xyzw;
  r2.xyzw = cb0[11].xxxx * r2.xyzw + v1.xyzw;
  r3.xyzw = t2.Sample(s0_s, v3.xy).xyzw;
  r1.y = r3.w * r2.w;
  r1.x = r1.y * r1.x;
  r1.y = saturate(w3.x);
  r1.z = r1.y * -2 + 3;
  r1.y = r1.y * r1.y;
  r1.y = r1.z * r1.y;
  r1.w = r1.x * r1.y;
  r1.xyz = r2.xyz * r1.www;
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}