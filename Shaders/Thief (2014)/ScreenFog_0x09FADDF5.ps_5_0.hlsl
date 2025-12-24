// ---- Created with 3Dmigoto v1.3.16 on Tue Jul 15 04:02:15 2025
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

cbuffer cb0 : register(b0)
{
  float4 cb0[28];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD4,
  float v1 : TEXCOORD1,
  float2 w1 : TEXCOORD9,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD6,
  float4 v4 : TEXCOORD7,
  float4 v5 : TEXCOORD10,
  float4 v6 : TEXCOORD8,
  uint v7 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = cb0[26].xy * v2.xy;
  r0.xy = frac(r0.xy);
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.x = dot(r0.xy, r0.xy);
  r0.x = r0.x * r0.x;
  r0.x = min(1, r0.x);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r0.x;
  r0.x = cb0[27].y * r0.x;
  r0.x = 0.100000001 * r0.x;
  r0.yz = cb0[27].xx * cb0[25].zw;
  r1.w = 1;
  r1.xy = w1.xy / v6.ww;
  r1.z = -r1.y;
  r2.x = dot(cb0[8].xyzw, r1.xzww);
  r2.y = dot(cb0[9].xyzw, r1.xzww);
  r1.xy = r1.xy * cb2[0].xy + cb2[0].wz;
  r0.yz = r2.xy * cb0[25].xy + r0.yz;
  r0.yz = t0.Sample(s0_s, r0.yz).xy;
  r0.yz = r0.yz * float2(2,2) + float2(-1,-1);
  r0.xy = r0.yz * r0.xx + v2.xy;
  r0.xyzw = t1.Sample(s1_s, r0.xy).xyzw;
  r0.xyz = r0.xyz * v0.xyz + cb0[24].xyz;
  o0.w = v0.w * r0.w;
  r2.xyz = t2.Sample(s2_s, r1.xy).xyz;
  r1.xyzw = t3.Sample(s3_s, r1.xy).xyzw;
  r0.w = dot(v5.xyzw, r1.xyzw);
  r1.x = dot(v4.xyz, r2.xyz);
  r0.w = r1.x + r0.w;
  r1.x = 1 + r0.w;
  r0.w = r0.w / r1.x;
  r1.xyz = v3.xyz * r0.www;
  r0.w = 1 + -r0.w;
  o0.xyz = r0.xyz * r0.www + r1.xyz;
  return;
}