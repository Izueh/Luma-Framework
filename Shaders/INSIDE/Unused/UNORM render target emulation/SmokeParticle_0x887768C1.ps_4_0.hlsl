Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[19];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float3 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = max(0, v2.z);
  r0.x = min(cb2[0].w, r0.x);
  r0.yz = v3.xy / v3.ww;
  r1.xyzw = t1.SampleLevel(s0_s, r0.yz, 0).xyzw;
  r0.y = cb1[5].z * r1.x + -v3.z;
  r0.y = saturate(cb0[18].x * r0.y);
  r1.w = v1.w * r0.y;
  r2.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r1.xyz = v1.xyz;
  r3.xyzw = r2.xyzw * r1.xyzw;
  r0.yzw = -r1.xyz * r2.xyz + cb2[0].xyz;
  r0.xyz = r0.xxx * r0.yzw + r3.xyz;
  r3.xyz = r0.xyz * r3.www;
  r0.xy = cb1[1].xx + v3.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r1.w = cb0[18].y * r0.w;
  o0.xyzw = r3.xyzw + -r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}