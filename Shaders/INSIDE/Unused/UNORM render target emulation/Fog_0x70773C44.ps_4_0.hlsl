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
  float4 cb0[19];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = cb1[1].xx + v1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r1.w = r0.w / cb0[18].z;
  r0.xy = v1.xy / v1.ww;
  r0.xyzw = t1.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r0.x = cb1[5].z * r0.x + -v1.z;
  r0.x = saturate(r0.x * v2.z + v2.w);
  r2.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r0.x = r2.w * r0.x;
  r0.w = cb0[17].w * r0.x;
  r0.xyz = cb0[17].xyz * r0.w;
  o0.xyzw = r0.xyzw + -r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}