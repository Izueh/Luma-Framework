#include "../Includes/Common.hlsl"
#include "../Includes/Reinhard.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[17];
}

void main(
  float4 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xyz = t0.Sample(s0_s, v0.xy).xyz;
  r1.xy = -cb0[15].zw + v0.xy;
  r2.xyz = t0.Sample(s0_s, r1.xy).xyz;
  r0.w = t1.Sample(s1_s, r1.xy).x;
  r0.xyz = r2.xyz + r0.xyz;
  r1.xyzw = -cb0[16].xyzw + v0.xyxy;
  r2.xyz = t0.Sample(s0_s, r1.xy).xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyz = t0.Sample(s0_s, r1.zw).xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  r2.xyz = cb0[12].yyy * r0.xyz;
  r0.x = GetLuminance(r0.xyz); // Luma: fixed BT.601 luminance
  r0.x = max(6.10351999e-005, r0.x);
  r2.xyz = r2.xyz / r0.xxx;
  r0.x = -cb0[13].w + r0.x;
  r0.x = max(0, r0.x);
  r0.xyz = r2.xyz * r0.xxx;
  r0.xyz = r0.xyz + r0.xyz;
  r2.x = 0.5 / cb0[12].x;
  r2.y = 1 + -cb2[2].y;
  r0.w = -r2.y + r0.w;
  r0.w = min(-9.99999996e-013, r0.w);
  r3.y = -cb2[2].x / r0.w;
  r0.w = t1.Sample(s1_s, r1.xy).x;
  r1.x = t1.Sample(s1_s, r1.zw).x;
  r1.x = r1.x + -r2.y;
  r1.x = min(-9.99999996e-013, r1.x);
  r3.w = -cb2[2].x / r1.x;
  r0.w = r0.w + -r2.y;
  r0.w = min(-9.99999996e-013, r0.w);
  r3.z = -cb2[2].x / r0.w;
  r0.w = t1.Sample(s1_s, v0.xy).x;
  r0.w = r0.w + -r2.y;
  r0.w = min(-9.99999996e-013, r0.w);
  r3.x = -cb2[2].x / r0.w;
  r1.xyzw = r3.xyzw + -r2.xxxx;
  r2.xyzw = saturate(cb0[12].xxxx * r3.xyzw); // Luma: remove these sat? They end up being waaaayyy too strong... Maybe we could TM it later? It doesn't seem to be necessary, they end up HDR anyway
  r0.w = dot(r2.xyzw, float4(0.25,0.25,0.25,0.25));
  r1.xyzw = saturate(cb0[12].xxxx * r1.xyzw);
  r1.x = dot(r1.xyzw, float4(0.25,0.25,0.25,0.25));
  r0.xyz = r1.xxx * r0.xyz;
  r1.xy = -cb0[10].xy + v0.xy;
  r1.xy = r1.xy / cb0[10].zw;
  r1.zw = float2(1,1) + -r1.xy;
  r1.x = r1.x * r1.z;
  r1.x = r1.x * r1.y;
  r1.x = r1.x * r1.w;
  r1.x = -r1.x * 8 + 1;
  r1.x = r1.x * r1.x;
  r1.y = -r1.x * r1.x + 1;
  r1.x = r1.x * r1.x;
  o0.w = max(r1.x, r0.w);
  r0.xyz = r1.yyy * r0.xyz;
  r1.xy = -v0.xy * cb0[11].zw + cb0[5].xy;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r0.w = 5 * r0.w;
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r0.w = r0.w * r0.w;
  o0.xyz = r0.www * r0.xyz;
  
  // Luma:
  //o0.rgb = Reinhard::ReinhardRange(o0.rgb, 0.5, -1.0, 1.0, false);
}