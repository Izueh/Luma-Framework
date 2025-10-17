Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[14];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float4 v4 : COLOR0,
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
  r0.x = cb0[13].y * r0.x;
  r0.x = exp2(r0.x);
  r0.y = r0.x * -2 + 1;
  r0.x = cb0[13].w * r0.y + r0.x;
  r0.y = 0.5;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.yzw = -cb0[12].xyz * v4.xyz + r0.xyz;
  r0.x = cb0[13].x * r0.x;
  r1.w = v4.w * r0.x;
  r2.xyz = cb0[12].xyz * v4.xyz;
  r0.xyz = cb0[13].zzz * r0.yzw + r2.xyz;
  r2.xyz = cb1[0].xyz + -r0.xyz;
  r0.w = max(0, v1.z);
  r0.w = min(cb1[0].w, r0.w);
  r0.xyz = r0.www * r2.xyz + r0.xyz;
  r1.xyz = r0.xyz * r1.www;
  r0.xy = float2(5.39870024,5.44210005) * v1.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}