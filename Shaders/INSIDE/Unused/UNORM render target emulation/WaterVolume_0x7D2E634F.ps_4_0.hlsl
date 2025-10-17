Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
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
  float4 cb0[26];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float3 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = cb1[1].xx + v1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r0.xyzw = float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) * r0.xyzw;
  r1.xy = v6.xy / v6.ww;
  r1.xy = float2(0.5,0.5) + r1.xy;
  r1.xyzw = t2.Sample(s0_s, r1.xy).xyzw;
  r1.x = cmp(0 < v6.z);
  r1.x = r1.x ? 1.000000 : 0;
  r1.x = r1.x * r1.w;
  r1.y = dot(v6.xyz, v6.xyz);
  r2.xyzw = t3.Sample(s1_s, r1.yy).xyzw;
  r1.x = r2.x * r1.x;
  r1.yz = v1.xy / v1.zz;
  r2.xyzw = t1.SampleLevel(s2_s, r1.yz, 0).xyzw;
  r1.y = cb1[5].z * r2.x + -v1.z;
  r1.y = saturate(4 * r1.y);
  r1.x = r1.y * r1.x;
  r2.xyzw = t0.Sample(s3_s, v4.zw).xyzw;
  r1.yz = float2(0.5,0.5) + -r2.zx;
  r2.xyzw = t0.Sample(s3_s, v4.xy).xyzw;
  r2.xy = float2(-0.5,-0.5) + r2.zx;
  r1.yz = -r2.xy + r1.yz;
  r1.yz = v3.ww * r1.yz + r2.xy;
  r2.xz = v5.xx * r1.yz;
  r1.y = dot(v2.xyz, v2.xyz);
  r1.y = rsqrt(r1.y);
  r1.yzw = v2.xyz * r1.yyy;
  r2.w = dot(r2.xz, r2.xz);
  r2.w = 1 + -r2.w;
  r2.y = sqrt(r2.w);
  r2.w = dot(r2.xyz, r1.yzw);
  r2.w = 1 + -r2.w;
  r2.w = sqrt(r2.w);
  r1.x = r2.w * r1.x;
  r2.w = 1 + -cb2[0].w;
  r2.w = max(v2.w, r2.w);
  r2.w = min(1, r2.w);
  r1.x = r2.w * r1.x;
  r2.w = 4 * cb0[23].w;
  r1.x = r2.w * r1.x;
  r2.w = dot(v3.xyz, v3.xyz);
  r2.w = rsqrt(r2.w);
  r1.yzw = v3.xyz * r2.www + r1.yzw;
  r3.xyz = v3.xyz * r2.www;
  r2.w = dot(r2.xyz, r3.xyz);
  r3.x = dot(r1.yzw, r1.yzw);
  r3.x = rsqrt(r3.x);
  r1.yzw = r3.xxx * r1.yzw;
  r1.y = dot(r2.xyz, r1.yzw);
  r1.y = log2(r1.y);
  r1.y = v5.y * r1.y;
  r1.y = exp2(r1.y);
  r1.z = r2.w * cb0[25].w + r1.y;
  r2.w = v5.z * r1.y;
  r2.xyz = cb0[6].xyz * r1.zzz;
  o0.xyzw = r2.xyzw * r1.xxxx + -r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}