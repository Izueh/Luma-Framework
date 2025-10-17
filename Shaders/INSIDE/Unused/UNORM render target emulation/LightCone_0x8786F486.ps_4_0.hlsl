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
  float4 cb0[22];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v2.xy / v2.ww;
  r0.xyzw = t1.SampleLevel(s2_s, r0.xy, 0).xyzw;
  r1.xyzw = t0.SampleBias(s1_s, v1.zw, cb0[21].z).xyzw;
  r0.yzw = float3(-0.5,-0.5,-0.5) + r1.zxy;
  r0.y = r0.y / cb0[6].x;
  r0.zw = r0.zw * cb0[21].yy + v1.xy;
  r2.xyzw = t2.Sample(s0_s, r0.zw).xyzw;
  r0.y = v2.z + r0.y;
  r0.x = cb1[5].z * r0.x + -r0.y;
  r0.y = (r0.x < 0);
  r0.x = saturate(cb0[6].x * r0.x);
  if (r0.y != 0) discard;
  r0.y = 1 + -cb0[7].w;
  r0.y = cb0[21].x * r0.y;
  r0.y = r1.w * r0.y;
  r0.y = r2.w * cb0[7].w + -r0.y;
  r0.z = (r0.y < 0);
  r0.x = r0.y * r0.x;
  r0.x = v4.x * r0.x;
  if (r0.z != 0) discard;
  r0.yz = cb1[1].xx + v2.xy;
  r0.yz = float2(0.987653971,0.987653971) + r0.yz;
  r0.yz = float2(5.39870024,5.44210005) * r0.yz;
  r0.yz = frac(r0.yz);
  r1.xy = float2(21.5351009,14.3136997) + r0.yz;
  r0.w = dot(r0.zy, r1.xy);
  r0.yz = r0.yz + r0.ww;
  r0.y = r0.y * r0.z;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.yyyy;
  r1.xyzw = frac(r1.xyzw);
  r1.xyzw = float4(0.00392156886,0.00392156886,0.00392156886,0.0322580636) * r1.xyzw;
  o0.xyzw = v3.xyzw * r0.xxxx + -r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}