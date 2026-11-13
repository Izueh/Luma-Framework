#include "Game/Common.hlsl"

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[123];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[23];
}

#define cmp -

void main(
  linear noperspective float4 v0 : TEXCOORD0,
  linear noperspective float4 v1 : TEXCOORD1,
  float4 v2 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;
  float4 renderResolution;
  if (LumaData.GameData.DrewUpscaling){
    renderResolution = LumaData.GameData.OutputResolution;
  } else {
    renderResolution = cb1[122];
  }
  r0.xy = v1.xy;
  r0.z = cb0[21].w * v1.w;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.y = r0.z * r0.z;
  r0.x = rcp(r0.x);
  r0.x = r0.y * r0.x;
  r0.y = r0.x * r0.x;
  r0.x = -r0.x * r0.x + 1;
  r0.z = cmp(cb0[21].y < 0);
  r0.x = r0.z ? r0.y : r0.x;
  r0.y = log2(abs(r0.x));
  r0.x = cmp(r0.x < 9.99999997e-07);
  r0.y = abs(cb0[21].y) * r0.y;
  r0.y = exp2(r0.y);
  r0.z = saturate(cb0[21].x);
  r0.y = r0.y * r0.z;
  r0.x = r0.x ? 0 : r0.y;
  r0.yz = -cb1[121].xy + v2.xy;
  r0.yz = renderResolution.zw * r0.yz;
  r0.yz = cb0[19].zw * r0.yz;
  r0.yz = max(float2(0.5,0.5), r0.yz);
  r1.xy = float2(-0.5,-0.5) + cb0[19].zw;
  r0.yz = min(r1.xy, r0.yz);
  r0.yz = cb0[1].zw * r0.yz;
  r0.yzw = t1.Sample(s1_s, r0.yz).xyz;
  r1.x = max(0.00100000005, cb0[20].y);
  r1.x = rcp(r1.x);
  r1.z = v1.z * r1.x;
  r1.xy = v1.xy;
  r1.x = dot(r1.xyz, r1.xyz);
  r1.y = r1.z * r1.z;
  r1.x = rcp(r1.x);
  r1.x = r1.y * r1.x;
  r1.x = r1.x * r1.x + -1;
  r1.x = cb0[20].x * r1.x + 1;
  r1.x = max(0, r1.x);
  r1.yzw = t0.SampleLevel(s0_s, v0.xy, 0).xyz;
  r2.xyz = r1.yzw * r1.xxx;
  r3.xy = float2(1,1) + -cb0[18].yz;
  r3.xzw = r3.xxx * r2.xyz;
  r0.yzw = r3.xzw * r3.yyy + r0.yzw;
  r0.yzw = -r1.yzw * r1.xxx + r0.yzw;
  r0.yzw = cb0[18].xxx * r0.yzw + r2.xyz;
  r1.x = dot(r0.yzw, float3(0.212599993,0.715200007,0.0722000003));
  r1.yzw = max(float3(0.00100000005,0.00100000005,0.00100000005), cb0[22].xyz);
  r2.x = dot(r1.yzw, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = rcp(r2.x);
  r1.yzw = r2.xxx * r1.yzw;
  r1.xyz = r1.yzw * r1.xxx;
  r1.w = max(0, cb0[21].z);
  r1.xyz = r1.xyz * r1.www + -r0.yzw;
  o0.xyz = r0.xxx * r1.xyz + r0.yzw;
  o0.w = 1;
  return;
}