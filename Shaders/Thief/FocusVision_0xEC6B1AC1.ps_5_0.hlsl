#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[24];
}

void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD8,
  float2 v2 : TEXCOORD9,
  uint v3 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2;
  r0.xy = v2.xy / v1.ww;
  r0.zw = float2(1.77769995,1) * r0.xy;
  r0.z = dot(r0.zw, r0.zw);
  r0.z = sqrt(r0.z);
  r0.z = cb0[23].x * r0.z;
  r0.z = 1.57079005 * r0.z;
  r0.z = cos(r0.z);
  r1.xy = r0.xy * r0.zz;
  r1.z = -r1.x;
  r1.xy = float2(1,1) + r1.zy;
  r1.xy = r1.xy * float2(1,-1) + float2(-1,1);
  r1.xy = r1.xy * cb2[0].xy + cb2[0].wz;
  r1.xyz = t0.Sample(s0_s, r1.xy).xyz;
#if POST_PROCESS_SPACE_TYPE == 0
  r1.rgb = gamma_sRGB_to_linear(r1.rgb);
#endif
  r0.w = saturate(-r0.z * 1.5 + 1);
  r0.w = r0.w * r0.w;
  r1.xyz = r1.xyz * r0.www;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r1.xyz = min(float3(0.0299999993,0.0299999993,0.0299999993), r1.xyz);
  r0.w = 1 + -r0.z;
  r0.z = saturate(r0.z + r0.z);
  r0.w = r0.w * cb0[23].y + 1;
  r2.xy = r0.xy / r0.ww;
  r0.x = dot(r0.xy, r0.xy);
  r2.z = -r2.y;
  r0.yw = float2(1,1) + r2.xz;
  r0.yw = r0.yw * float2(1,-1) + float2(-1,1);
  r0.yw = r0.yw * cb2[0].xy + cb2[0].wz;
  r2.xyz = t0.Sample(s0_s, r0.yw).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.yzw = r1.xyz * r0.zzz;
  r1.x = 1.10000002 * r0.x;
  r0.x = r0.x * 1.10000002 + 0.200000003;
  r0.x = r1.x / r0.x;
  r1.xyz = r0.yzw * r0.xxx;
  r0.xyz = r1.xyz * float3(-1,-1,-0.899999976) + r0.yzw;
  o0.xyz = cb0[22].xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
#if POST_PROCESS_SPACE_TYPE == 0 // TODO: handle all post process after this one to make it work in gamma space! Also make FXAA mandatory?
  o0.rgb = linear_to_sRGB_gamma(o0.rgb);
#endif
}