// ---- Created with 3Dmigoto v1.3.16 on Thu Oct 16 06:07:52 2025
Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture3D<float4> t0 : register(t0);

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD3,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = -cb0[2].x + 1;
  r0.x = cb0[2].y * r0.x + cb0[2].x;
  r0.y = cb0[1].w * r0.x;
  r0.xzw = r0.xxx * float3(0.333999991,0,-1) + float3(0.666000009,1,1);
  r0.y = r0.y * 39 + 1;
  r1.x = cb0[2].z * r0.y;
  r0.y = cb0[1].z * r0.y;
  r1.yz = v2.xy * float2(1,-1) + float2(0,1);
  r2.xy = r1.yz + r1.yz;
  r1.yzw = t3.Sample(s3_s, r1.yz).xyz;
  r2.z = t2.Sample(s2_s, r2.xy).x;
  r2.x = t1.Sample(s1_s, r2.xy).x;
  r2.y = 1 + -r2.z;
  r0.x = r2.z * r0.x;
  r2.y = -r2.y * 0.75 + 1;
  r2.y = saturate(r2.y + -r2.x);
  r2.x = 0.5 * r2.x;
  r2.x = 1 + -abs(r2.x);
  r1.x = r2.y * r1.x;
  r0.y = r2.y * r0.y;
  r3.z = cb0[3].z * r0.y;
  r3.x = cb0[3].z * r1.x;
  r3.yw = float2(0,0);
  r2.yz = cb0[3].xy + v2.xy;
  r3.xy = r2.yz * cb0[3].zw + r3.xy;
  r3.zw = r2.yz * cb0[3].zw + r3.zw;
  r2.yz = cb0[3].zw * r2.yz;
  r4.x = t6.Sample(s6_s, r3.xy).x;
  r5.x = t4.Sample(s4_s, r3.xy).x;
  r4.z = t6.Sample(s6_s, r3.zw).z;
  r5.z = t4.Sample(s4_s, r3.zw).z;
  r4.y = t6.Sample(s6_s, r2.yz).y;
  r3.xyz = t4.Sample(s4_s, r2.yz).xyz;
  r0.y = t5.Sample(s5_s, r2.yz).x;
  r0.y = r0.y * 2 + cb0[4].x;
  r0.y = -1 + r0.y;
  r0.y = cb0[4].y / r0.y;
  r0.y = cb0[0].z * r0.y;
  r5.y = r3.y;
  r2.yzw = -r5.xyz + r4.xyz;
  r2.yzw = r0.xxx * r2.yzw + r5.xyz;
  r2.yzw = t0.Sample(s0_s, r2.yzw).xyz;
  r2.xyz = r2.yzw * r2.xxx;
  r0.xzw = r2.xyz * r0.zww + -r3.xyz;
  r1.x = cb0[0].y + 9.99999975e-006;
  r0.y = saturate(r0.y / r1.x);
  r0.y = r0.y * r0.y;
  r1.x = cb0[1].y + -cb0[0].w;
  r1.x = cb0[1].x * r1.x + cb0[0].w;
  r0.y = r1.x * r0.y;
  r0.y = cb0[0].x * r0.y;
  r0.xyz = r0.yyy * r0.xzw + r3.xyz;
  r1.xyz = r1.yzw + -r0.xyz;
  o0.xyz = cb0[2].www * r1.xyz + r0.xyz;
  o0.w = 1;
}