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
  float4 cb0[7];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float3 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = dot(v4.xyz, v4.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v4.xyz * r0.xxx;
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v5.xyz * r0.w;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.y = 0.01 + cb0[6].z;
  r0.y = max(cb0[6].y, r0.y);
  r0.x = abs(r0.x) + -r0.y;
  r0.y = -cb0[6].z + r0.y;
  r0.x = saturate(r0.x / r0.y);
  r1.xyzw = t1.Sample(s0_s, v2.xy).xyzw;
  r0.x = r1.w * r0.x;
  r0.yz = v3.xy / v3.ww;
  r1.xyzw = t0.SampleLevel(s1_s, r0.yz, 0).xyzw;
  r0.y = cb1[5].z * r1.x + -v3.z;
  r0.y = saturate(cb0[6].x * r0.y);
  r0.y = v1.w * r0.y;
  r0.w = r0.y * r0.x;
  r0.xyz = v1.xyz * r0.w;
  r1.xy = cb1[1].xx + v3.xy;
  r1.xy = float2(0.987770021,0.987770021) + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.x;
  r1.xyzw = frac(r1.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  r2.w = r1.w / cb0[6].w;
  o0.xyzw = -r2.xyzw + r0.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}