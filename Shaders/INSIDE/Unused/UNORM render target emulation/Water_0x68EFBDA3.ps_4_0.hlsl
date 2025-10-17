Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[18];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float v2 : TEXCOORD1,
  float3 w2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  nointerpolation float3 v4 : TEXCOORD4,
  float4 v5 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyzw = t0.Sample(s1_s, v3.zw).xyzw;
  r0.xyz = r0.zxw * float3(-1,-1,1) + float3(0.5,0.5,0);
  r1.xyzw = t0.Sample(s1_s, v3.xy).xyzw;
  r1.xyz = float3(-0.5,-0.5,0) + r1.zxw;
  r0.xyz = -r1.xyz + r0.xyz;
  r0.xyz = v2.xxx * r0.xyz + r1.xyz;
  r1.xy = v4.zz * r0.xy;
  r0.xy = v1.xy / v1.ww;
  r2.xy = r0.xy * cb0[17].xy + cb0[17].zw;
  r3.xyzw = t2.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r0.x = cb1[5].z * r3.x + -v1.z;
  r0.x = saturate(4 * r0.x);
  r0.yw = r1.xy * v4.xy + r2.xy;
  r3.xyzw = t1.SampleLevel(s2_s, r0.yw, 0).xyzw;
  r4.xyzw = t1.SampleLevel(s2_s, r2.xy, 0).xyzw;
  r0.y = r4.w * r3.w;
  r2.zw = v4.xy * r1.xy;
  r0.yw = r2.zw * r0.yy + r2.xy;
  r2.xyzw = t1.SampleLevel(s2_s, r0.yw, 0).xyzw;
  r3.xyz = v5.xyz + -r2.xyz;
  r0.y = saturate(-cb1[5].y + v1.z);
  r0.x = r0.x * r0.y;
  r0.y = r0.z * r0.x;
  r0.y = saturate(v5.w * r0.y);
  r0.z = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.w = r0.z * r0.y;
  r0.y = -r0.z * r0.y + 1;
  r2.xyz = r0.www * r3.xyz + r2.xyz;
  r1.z = 1;
  r0.z = dot(r1.xyz, w2.xyz);
  r1.x = dot(w2.xyz, w2.xyz);
  r1.x = rsqrt(r1.x);
  r0.z = saturate(r0.z * r1.x + 1);
  r0.z = sqrt(r0.z);
  r0.x = r0.z * r0.x;
  r0.w = r0.x * r0.y + r0.w;
  r0.xyz = r2.xyz * r0.www;
  r1.xy = cb1[1].xx + v1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  o0.xyzw = -r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}