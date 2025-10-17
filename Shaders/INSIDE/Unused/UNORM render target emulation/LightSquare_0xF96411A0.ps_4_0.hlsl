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
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[4];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD1,
  float3 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.w = dot(v2.xyz, v2.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v2.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, r1.xyz));
  r0.x = log2(r0.x);
  r0.x = cb0[3].z * r0.x;
  r0.x = exp2(r0.x);
  r0.y = 1 + -r0.x;
  r0.y = r0.y + -r0.x;
  r0.x = cb0[3].y * r0.y + r0.x;
  r0.y = 0;
  r0.xyzw = t0.Sample(s1_s, r0.xy).xyzw;
  r0.yzw = -cb0[0].xyz + r0.xyz;
  r0.x = cb0[1].x * r0.x;
  r0.yzw = cb0[3].xxx * r0.yzw + cb0[0].xyz;
  r0.xyz = r0.xxx * r0.yzw + -cb2[0].xyz;
  r0.w = 1 + -cb2[0].w;
  r0.w = max(v3.w, r0.w);
  r0.w = min(1, r0.w);
  r0.xyz = r0.www * r0.xyz + cb2[0].xyz;
  r1.xy = cb1[1].xx + v1.zw;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r0.w = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r0.ww;
  r0.w = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.www;
  r1.xyz = frac(r1.xyz);
  r1.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  r2.xyzw = t1.Sample(s0_s, v1.xy).xyzw;
  o0.xyz = r0.xyz * r2.xyz + -r1.xyz;
  o0.w = 1;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
}