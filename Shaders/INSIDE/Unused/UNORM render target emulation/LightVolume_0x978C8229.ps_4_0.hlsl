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

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v4.xy / v4.ww;
  r0.xyzw = t3.SampleLevel(s3_s, r0.xy, 0).xyzw;
  r0.x = cb1[5].z * r0.x + -v4.z;
  r0.x = saturate(cb0[7].x * r0.x);
  r1.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r2.xyzw = t1.Sample(s2_s, v2.zw).xyzw;
  r1.xyzw = r2.xyzw * r1.xyzw;
  r1.xyzw = cb0[11].yyyy * r1.xyzw;
  r1.xyzw = r1.xyzw * cb0[6].xyzw + -v1.xyzw;
  r1.xyzw = cb0[11].xxxx * r1.xyzw + v1.xyzw;
  r2.xyzw = t2.Sample(s0_s, v3.xy).xyzw;
  r0.y = r2.w * r1.w;
  r0.w = r0.y * r0.x;
  r0.xyz = r1.xyz * r0.www;
  r1.xy = cb1[1].xx + v4.xy;
  r1.xy = float2(0.987653971,0.987653971) + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  r2.w = cb0[11].z * r1.w;
  o0.xyzw = -r2.xyzw + r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}