Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float v1 : TEXCOORD0,
  float3 w1 : TEXCOORD2,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD3,
  float w3 : TEXCOORD7,
  nointerpolation float v4 : TEXCOORD4,
  nointerpolation float3 w4 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = v0.xy * cb1[6].zw + v0.ww;
  r0.xy = cb1[1].xx + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xyzw = frac(r1.xyzw);
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, v2.zw).xyzw;
  r1.xyzw = r2.wxwx + r1.wxwx;
  r1.xyzw = r1.xyzw * float4(2,2,2,2) + float4(-2,-2,-2,-2);
  r2.xy = float2(-1,-1) + cb1[6].zw;
  r2.xy = v0.xy * r2.xy;
  r2.xy = r2.xy * cb0[11].xy + cb0[11].zw;
  r2.zw = r1.xy * w4.xy + r2.xy;
  r1.xyzw = w4.zxzy * r1.zxwy;
  r3.xyzw = t1.SampleLevel(s1_s, r2.zw, 0).xyzw;
  r4.xyzw = t1.SampleLevel(s1_s, r2.xy, 0).xyzw;
  r2.z = min(r4.w, r3.w);
  r2.xy = r1.yw * r2.zz + r2.xy;
  r2.xyzw = t1.SampleLevel(s1_s, r2.xy, 0).xyzw;
  r3.xyz = -cb0[9].yyy + r2.xyz;
  r3.xyz = cb0[9].zzz * r3.xyz;
  r3.xyz = max(float3(0,0,0), r3.xyz);
  r2.xyz = r3.xyz * r3.xyz + r2.xyz;
  r1.w = dot(r1.xz, r1.xz);
  r1.y = 1 + -r1.w;
  r1.x = dot(r1.xyz, w1.xyz);
  r1.y = dot(w1.xyz, w1.xyz);
  r1.y = rsqrt(r1.y);
  r1.x = saturate(r1.x * -r1.y + 1);
  r1.x = log2(r1.x);
  r1.x = cb0[7].w * r1.x;
  r1.x = exp2(r1.x);
  r3.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r1.y = cb1[7].z * r3.x + cb1[7].w;
  r1.y = 1 / r1.y;
  r1.y = -v1.x + r1.y;
  r1.y = saturate(v4.x * r1.y);
  r1.z = dot(v3.xyz, v3.xyz);
  r1.z = saturate(-0.75 + r1.z);
  r1.y = r1.y * r1.z;
  r1.z = saturate(-cb1[5].y + v1.x);
  r1.y = r1.y * r1.z;
  r1.y = w3.x * r1.y;
  r1.z = r1.x * r1.y;
  r2.xyz = r2.xyz * r1.zzz;
  r1.z = saturate(v1.x * -cb0[10].x + 1);
  r1.w = r1.z * r1.z;
  r1.z = -r1.z * 2 + 3;
  r1.z = r1.w * r1.z;
  r1.w = cb0[9].w * r1.z;
  r1.z = -r1.z * cb0[9].w + 1;
  r1.x = r1.x * r1.z + r1.w;
  r2.w = r1.x * r1.y;
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.100000001) + r2.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}