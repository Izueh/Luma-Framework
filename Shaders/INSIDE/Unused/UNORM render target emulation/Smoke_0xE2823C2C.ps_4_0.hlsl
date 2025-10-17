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
  float4 cb0[23];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  float w3 : TEXCOORD7,
  nointerpolation float v4x : TEXCOORD3,
  nointerpolation float v4y : TEXCOORD4,
  nointerpolation float v4z : TEXCOORD5,
  nointerpolation float v4w : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v0.xy + v0.zz;
  r0.xy = cb1[1].xx + r0.xy;
  r0.xy = cb0[22].zz + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.x = dot(v3.xy, v3.xy);
  r1.x = r1.x * r1.x;
  r1.x = r1.x * r1.x;
  r1.x = saturate(r1.x * -cb0[14].z + cb0[14].z);
  r2.xyzw = t0.SampleLevel(s1_s, v1.zw, v4x).xyzw;
  r1.yz = float2(-0.5,-0.5) + r2.xy;
  r1.xy = r1.yz * r1.xx + v1.xy;
  r1.xyzw = t1.Sample(s0_s, r1.xy).xyzw;
  r1.x = v2.w * r1.w;
  r2.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r1.y = cb1[7].z * r2.x + cb1[7].w;
  r1.y = 1 / r1.y;
  r1.y = saturate(r1.y * v4z + -w3.x);
  r1.x = r1.x * r1.y;
  r2.xyz = v2.xyz * r1.xxx;
  r2.w = v4w * r1.x;
  r1.x = 0.00392156886;
  r1.w = v4y;
  o0.xyzw = -r0.xyzw * r1.xxxw + r2.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}