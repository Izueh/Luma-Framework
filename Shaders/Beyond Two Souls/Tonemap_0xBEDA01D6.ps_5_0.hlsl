// ---- Created with 3Dmigoto v1.3.16 on Thu Oct 16 06:17:55 2025
Texture2D<float4> t10 : register(t10);

Texture2D<float4> t9 : register(t9);

Texture2D<float4> t8 : register(t8);

Texture2D<float4> t7 : register(t7);

Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s10_s : register(s10);

SamplerState s9_s : register(s9);

SamplerState s8_s : register(s8);

SamplerState s7_s : register(s7);

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s4_s : register(s4);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[6];
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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.y = cb0[2].z * 0.00392100029;
  r0.z = 0;
  r0.xyz = t3.Sample(s3_s, r0.yz).xyz;
  r1.xyz = cb0[3].xxx * r0.xyz;
  r0.w = dot(r1.xyz, float3(0.211999997,0.714999974,0.0719999969));
  r0.xyz = r0.xyz * cb0[3].xxx + -r0.www;
  r0.xyz = cb0[2].xxx * r0.xyz + r0.www;
  r1.xyz = cb0[1].zzz * r0.xyz;
  r0.xyz = cb0[1].xxx * r0.xyz;
  sincos(cb0[0].w, r2.x, r3.x);
  r2.z = r2.x;
  r2.y = r3.x;
  r3.xyzw = v2.xyxy * float4(1,-1,1,-1) + float4(0,1,-1,0);
  r4.y = dot(r3.wz, r2.yz);
  r2.x = sin(-cb0[0].w);
  r4.x = dot(r3.wz, r2.xy);
  r2.xy = float2(1,1) + r4.xy;
  r0.w = t2.Sample(s2_s, r2.xy).x;
  r0.xyz = r0.www * r0.xyz;
  sincos(cb0[1].y, r2.x, r4.x);
  r2.z = r2.x;
  r2.y = r4.x;
  r4.y = dot(r3.wz, r2.yz);
  r2.x = sin(-cb0[1].y);
  r4.x = dot(r3.wz, r2.xy);
  r2.xy = float2(1,1) + r4.xy;
  r0.w = t2.Sample(s2_s, r2.xy).x;
  r0.xyz = saturate(r0.www * r1.xyz + r0.xyz);
  r1.x = cb0[2].z * 0.00392100029 + -cb0[2].y;
  r1.y = 0;
  r1.xyz = t3.Sample(s3_s, r1.xy).xyz;
  r0.w = cb0[3].x * cb0[2].w;
  r2.xyz = r1.xyz * r0.www;
  r1.w = dot(r2.xyz, float3(0.211999997,0.714999974,0.0719999969));
  r1.xyz = r1.xyz * r0.www + -r1.www;
  r0.w = cb0[2].x * cb0[1].w;
  r1.xyz = r0.www * r1.xyz + r1.www;
  r0.w = t7.Sample(s7_s, r3.xy).x;
  r1.xyz = r1.xyz * r0.www;
  r0.w = 1 + -abs(r0.w);
  r1.xyz = r1.xyz + r1.xyz;
  r0.xyz = r0.xyz * cb0[0].zzz + r1.xyz;
  r1.xyzw = r3.xyxy * float4(0.5,0.5,0.800000012,0.800000012) + cb0[4].yyxx;
  r1.xy = t6.Sample(s6_s, r1.xy).xy;
  r1.zw = t6.Sample(s6_s, r1.zw).xy;
  r1.xy = r1.xy + -r1.zw;
  r1.xy = r1.xy * float2(0.5,0.5) + r1.zw;
  r1.xy = saturate(float2(0.00999999978,0.00999999978) * r1.xy);
  r1.z = t8.Sample(s8_s, r3.xy).x;
  r0.w = r1.z * r0.w;
  r1.xy = r0.ww * r1.xy;
  r1.zw = r3.xy + r3.xy;
  r1.z = t5.Sample(s5_s, r1.zw).x;
  r1.xy = r1.xy * r1.zz;
  r1.z = 1 + -r1.z;
  r1.z = -r1.z * 0.666999996 + 1;
  r1.xy = cb0[3].ww * r1.xy;
  r1.xy = cb0[5].zw * r1.xy;
  r2.xy = cb0[5].xy + v2.xy;
  r1.xy = r2.xy * cb0[5].zw + r1.xy;
  r2.xyz = t10.Sample(s10_s, r1.xy).xyz;
  r1.xyw = t9.Sample(s9_s, r1.xy).xyz;
  r2.xyz = r2.xyz + -r1.xyw;
  r2.w = saturate(cb0[3].y * 0.999989986);
  r2.w = r2.w * -0.5 + 0.5;
  r2.w = 1 + -r2.w;
  r4.x = cb0[3].z + r3.x;
  r3.xyz = t1.Sample(s1_s, r3.xy).xyz;
  r4.y = -v2.y;
  r4.xy = float2(-0.5,1) + r4.xy;
  r3.w = t4.Sample(s4_s, r4.xy).x;
  r4.x = t0.Sample(s0_s, r4.xy).x;
  r4.x = cb0[0].x * r4.x;
  r4.x = 0.5 * r4.x;
  r3.w = 1 + -r3.w;
  r2.w = -r3.w * r2.w + 1;
  r0.w = saturate(r2.w * r0.w);
  r2.xyz = r0.www * r2.xyz + r1.xyw;
  r0.xyz = r2.xyz * r1.zzz + r0.xyz;
  r0.xyz = r0.xyz + -r1.xyw;
  r0.xyz = cb0[3].www * r0.xyz + r1.xyw;
  r0.xyz = r3.xyz * float3(0.5,0.5,0.5) + r0.xyz;
  o0.xyz = cb0[0].yyy * r0.xyz + r4.xxx;
  o0.w = 1;
  return;
}