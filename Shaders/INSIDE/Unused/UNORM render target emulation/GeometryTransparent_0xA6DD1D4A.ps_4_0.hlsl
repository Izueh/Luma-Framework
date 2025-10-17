TextureCube<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float4 v4 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = dot(v2.xyz, v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v2.xyz * r0.xxx;
  r0.w = dot(v3.xyz, v3.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v3.xyz * r0.www;
  r0.w = dot(r1.xyz, r0.xyz);
  r1.w = r0.w + r0.w;
  r0.w = saturate(r0.w);
  r0.w = log2(r0.w);
  r0.w = cb0[13].y * r0.w;
  r0.w = exp2(r0.w);
  r0.xyz = r0.xyz * -r1.www + r1.xyz;
  r1.xyzw = t1.Sample(s1_s, r0.xyz).xyzw;
  r0.xyz = cb0[14].xyz * r1.xyz;
  r1.x = r0.w * -2 + 1;
  r1.x = cb0[13].w * r1.x + r0.w;
  r0.w = 1 + -cb0[14].w;
  r0.w = r1.x * r0.w + cb0[14].w;
  r1.y = 0.5;
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.yzw = -cb0[12].xyz * v4.xyz + r1.xyz;
  r2.w = cb0[13].x * r1.x;
  r3.xyz = cb0[12].xyz * v4.xyz;
  r1.xyz = cb0[13].zzz * r1.yzw + r3.xyz;
  r0.xyz = r0.xyz * r0.www + r1.xyz;
  r0.xyz = -cb1[0].xyz + r0.xyz;
  r0.w = 1 + -cb1[0].w;
  r0.w = max(v2.w, r0.w);
  r0.w = min(1, r0.w);
  r2.xyz = r0.www * r0.xyz + cb1[0].xyz;
  r0.xy = float2(5.39870024,5.44210005) * v1.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r2.xyzw;

  // NOTE: this is probably not needed as it's for opaque geometry and the code almost never outputs beyond 0-1 (and when it does it's mostly fine)
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}