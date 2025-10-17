Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = cb1[1].xx + v2.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.xxx;
  r0.xyz = frac(r0.xyz);
  r1.xyzw = t1.Sample(s0_s, v1.xy).xyzw;
  r1.xy = v2.xy / v2.ww;
  r2.xyzw = t0.Sample(s2_s, r1.xy).xyzw;
  r0.w = r2.w * r1.w;
  o0.w = cb0[6].y * r2.w;
  r1.x = cb0[6].x * r0.w;
  r1.y = 0.5;
  r1.xyzw = t2.Sample(s1_s, r1.xy).xyzw;
  o0.xyz = -r0.xyz * float3(0.00392156886,0.00392156886,0.00392156886) + r1.xyz;
  
  // LUMA: UNORM blends emulation
  o0.w = saturate(o0.w);
  o0.rgb = max(o0.rgb, 0.0);
}