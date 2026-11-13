#include "Game/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[22];
}

// 3Dmigoto declarations
#define cmp -

void main(
  linear noperspective float4 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;
  float2 resolution;

  if (LumaData.GameData.DrewUpscaling){
    resolution = LumaData.GameData.OutputResolution.xy;
  } else {
    resolution = cb0[20].zw;
  }

  r0.xy = floor(v1.xy);
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.zw = rcp(cb0[21].zw);
  r0.zw = resolution.xy * r0.zw;
  r0.xy = r0.xy * r0.zw + float2(0.5,0.5);
  r1.xyzw = r0.zwzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r0.zw = trunc(r0.xy);
  r0.xy = r0.xy + -r0.zw;
  r2.xy = max(float2(0,0), r0.xy);
  r3.xyzw = r0.xyxy + r1.xyzw;
  r0.x = r1.z * r1.w;
  r0.x = rcp(r0.x);
  r1.xyzw = min(float4(1,1,2,2), r3.zwzw);
  r3.xyzw = min(float4(4,4,3,3), r3.xyzw);
  r3.xyzw = float4(-3,-3,-2,-2) + r3.xyzw;
  r3.xyzw = max(float4(0,0,0,0), r3.xyzw);
  r1.xy = r1.xy + -r2.xy;
  r1.zw = float2(-1,-1) + r1.zw;
  r2.xy = max(float2(0,0), r1.zw);
  r1.xy = max(float2(0,0), r1.xy);
  r4.xy = r1.xy + r2.xy;
  r1.yw = max(float2(9.99999975e-05,9.99999975e-05), r4.xy);
  r1.yw = rcp(r1.yw);
  r1.yw = r2.xy * r1.yw + r0.zw;
  r5.xy = float2(-1,-1) + resolution.xy;
  r5.xy = max(float2(1,1), r5.xy);
  r5.zw = rcp(r5.xy);
  r1.yw = r5.zw * r1.yw;
  r1.yw = float2(0.5,0.5) * r1.yw;
  r1.yw = frac(r1.yw);
  r1.yw = r1.yw * float2(2,2) + float2(-1,-1);
  r1.yw = float2(1,1) + -abs(r1.yw);
  r1.yw = r1.yw * r5.xy + float2(0.5,0.5);
  r1.yw = cb0[0].zw * r1.yw;
  r6.xyz = t0.SampleLevel(s0_s, r1.yw, 0).xyz;
  r6.xyz = min(float3(65504,65504,65504), r6.xyz);
  r6.xyz = r6.xyz * r4.xxx;
  r6.xyz = r6.xyz * r4.yyy;
  r7.x = 2 * r3.z;
  r7.y = 0;
  r2.zw = r3.xy;
  r1.yw = r2.zy * float2(3,1) + r7.xy;
  r2.xy = float2(1,3) * r2.xw;
  r7.xyzw = r3.zwzw + r3.xyzw;
  r4.zw = r7.xy;
  r8.xyzw = max(float4(9.99999975e-05,9.99999975e-05,9.99999975e-05,9.99999975e-05), r4.zyxw);
  r8.xyzw = rcp(r8.xyzw);
  r1.yw = r1.yw * r8.xy + r0.zw;
  r1.yw = r5.zw * r1.yw;
  r1.yw = float2(0.5,0.5) * r1.yw;
  r1.yw = frac(r1.yw);
  r1.yw = r1.yw * float2(2,2) + float2(-1,-1);
  r1.yw = float2(1,1) + -abs(r1.yw);
  r1.yw = r1.yw * r5.xy + float2(0.5,0.5);
  r1.yw = cb0[0].zw * r1.yw;
  r9.xyz = t0.SampleLevel(s0_s, r1.yw, 0).xyz;
  r9.xyz = min(float3(65504,65504,65504), r9.xyz);
  r9.xyz = r9.xyz * r7.xxx;
  r4.yzw = r9.xyz * r4.yyy;
  r4.yzw = r4.yzw * r0.xxx;
  r4.yzw = r6.xyz * r0.xxx + r4.yzw;
  r1.z = r3.w;
  r1.yw = r3.xy * float2(3,3) + r7.zw;
  r1.xz = r1.xz * float2(0,2) + r2.xy;
  r1.xz = r1.xz * r8.zw + r0.zw;
  r1.xz = r5.zw * r1.xz;
  r1.xz = float2(0.5,0.5) * r1.xz;
  r1.xz = frac(r1.xz);
  r1.xz = r1.xz * float2(2,2) + float2(-1,-1);
  r1.xz = float2(1,1) + -abs(r1.xz);
  r1.xz = r1.xz * r5.xy + float2(0.5,0.5);
  r1.xz = cb0[0].zw * r1.xz;
  r2.xyz = t0.SampleLevel(s0_s, r1.xz, 0).xyz;
  r2.xyz = min(float3(65504,65504,65504), r2.xyz);
  r2.xyz = r2.xyz * r4.xxx;
  r2.xyz = r2.xyz * r7.yyy;
  r2.xyz = r2.xyz * r0.xxx + r4.yzw;
  r1.xz = max(float2(9.99999975e-05,9.99999975e-05), r7.xy);
  r1.xz = rcp(r1.xz);
  r0.yz = r1.yw * r1.xz + r0.zw;
  r0.yz = r5.zw * r0.yz;
  r0.yz = float2(0.5,0.5) * r0.yz;
  r0.yz = frac(r0.yz);
  r0.yz = r0.yz * float2(2,2) + float2(-1,-1);
  r0.yz = float2(1,1) + -abs(r0.yz);
  r0.yz = r0.yz * r5.xy + float2(0.5,0.5);
  r0.yz = cb0[0].zw * r0.yz;
  r0.yzw = t0.SampleLevel(s0_s, r0.yz, 0).xyz;
  r0.yzw = min(float3(65504,65504,65504), r0.yzw);
  r0.yzw = r0.yzw * r7.xxx;
  r0.yzw = r0.yzw * r7.yyy;
  r0.xyz = r0.yzw * r0.xxx + r2.xyz;
  r0.w = max(0.00100000005, cb0[19].y);
  r0.w = rcp(r0.w);
  r1.z = v0.z * r0.w;
  r1.xy = v0.xy;
  r0.w = dot(r1.xyz, r1.xyz);
  r1.x = r1.z * r1.z;
  r0.w = rcp(r0.w);
  r0.w = r1.x * r0.w;
  r0.w = r0.w * r0.w + -1;
  r0.w = cb0[19].x * r0.w + 1;
  r0.w = max(0, r0.w);
  o0.xyz = r0.xyz * r0.www;
  o0.w = 1;
  return;
}