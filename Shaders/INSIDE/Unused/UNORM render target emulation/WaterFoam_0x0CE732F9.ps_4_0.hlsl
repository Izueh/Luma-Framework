Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[20];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xyzw = t1.Sample(s0_s, v3.xy).xyzw;
  r1.xyzw = t1.Sample(s0_s, v3.zw).xyzw;
  r0.xyzw = r1.wxwx + r0.wxwx;
  r0.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  r0.xyzw = cb0[13].wwzz * r0.xyzw;
  r0.xy = float2(1.77999997,1.77999997) * r0.xy;
  r1.x = v2.z * v2.z;
  r1.y = r1.x * r1.x;
  r1.x = r1.x * 8 + -4;
  r1.x = saturate(4 + -abs(r1.x));
  r1.y = 6.28000021 * r1.y;
  r1.y = sin(r1.y);
  r1.zw = v2.xy * r1.yy;
  r0.zw = v2.xy * r1.yy + r0.zw;
  r0.z = dot(r0.zw, v4.xz);
  r0.z = saturate(v4.y + r0.z);
  r0.z = log2(r0.z);
  r0.xy = r1.zw * float2(0.200000003,0.200000003) + r0.xy;
  r1.yz = v1.xy / v1.ww;
  r2.xyzw = t0.SampleLevel(s2_s, r1.yz, 0).xyzw;
  r0.w = cb1[7].z * r2.x + cb1[7].w;
  r0.w = 1 / r0.w;
  r0.w = saturate(-v1.z + r0.w);
  r2.yz = r0.xy * r0.ww;
  r0.x = r1.x * r0.w;
  r3.w = v2.w * r0.x;
  r0.x = cb1[6].y / cb1[6].x;
  r2.x = r2.y * r0.x;
  r0.xy = -r2.xz + r1.yz;
  r0.xy = r0.xy * cb0[19].xy + cb0[19].zw;
  r1.xyzw = t2.SampleLevel(s1_s, r0.xy, 0).xyzw;
  r0.xyw = -cb0[15].xxx + r1.xyz;
  r0.xyw = max(float3(0,0,0), r0.xyw);
  r0.xyw = cb0[14].www * r0.xyw;
  r0.xyw = r0.xyw * r0.xyw + r1.xyz;
  r1.x = 0.5 * cb0[14].x;
  r0.z = r1.x * r0.z;
  r0.z = exp2(r0.z);
  r0.xyz = r0.xyw * r0.zzz;
  r3.xyz = r0.xyz * r3.www;
  r0.xy = cb1[1].xx + v1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r0.xyzw = r0.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  o0.xyzw = r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r3.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}