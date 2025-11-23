// ---- Created with 3Dmigoto v1.4.1 on Thu Oct 23 03:20:51 2025
cbuffer cb2 : register(b2)
{
  float4 cb2[17];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[10];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[63];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : ATTRIBUTE0,
  float3 v1 : ATTRIBUTE1,
  float4 v2 : ATTRIBUTE2,
  float4 v3 : ATTRIBUTE4,
  float2 v4 : ATTRIBUTE15,
  uint v5 : SV_InstanceID0,
  out float4 o0 : TEXCOORD0,
  out float4 o1 : TEXCOORD3,
  out float4 o2 : TEXCOORD6,
  out float4 o3 : TEXCOORD7,
  out float4 o4 : VertexContextVector0,
  out float4 o5 : SV_POSITION0,
  out float o6 : SV_CullDistance0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  o0.xyzw = v3.xyzw;
  o1.xy = v4.xy * cb2[16].xy + cb2[16].zw;
  o1.zw = float2(0,0);
  r0.xyz = v1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.xyzw = v2.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r2.xyz = r1.yzx * r0.xyz;
  r0.xyz = r1.xyz * r0.yzx + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r0.xyz * r1.www;
  r2.xyz = cb1[1].yzx * r0.zzz;
  r2.xyz = r0.yyy * cb1[0].yzx + r2.xyz;
  r2.xyz = r0.xxx * cb1[2].yzx + r2.xyz;
  r3.xyz = cb1[1].yzx * r1.yyy;
  r3.xyz = r1.xxx * cb1[0].yzx + r3.xyz;
  r3.xyz = r1.zzz * cb1[2].yzx + r3.xyz;
  r4.xyz = r3.xyz * r2.yzx;
  r3.xyz = r2.xyz * r3.yzx + -r4.xyz;
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r3.xyz * r0.www;
  r4.xyz = r0.xyz * r1.yzx;
  r0.xyz = r0.zxy * r1.zxy + -r4.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r0.xyz * r1.www;
  r1.w = cb1[9].w * r1.w;
  r4.xyz = cb1[1].xyz * r0.yyy;
  r0.xyw = r0.xxx * cb1[0].xyz + r4.xyz;
  r0.xyz = r0.zzz * cb1[2].xyz + r0.xyw;
  r0.w = dot(r0.xyz, r3.xyz);
  r0.w = cmp(r0.w < 0);
  r4.xyz = r0.zxy * r2.xyz;
  r0.xyz = r0.yzx * r2.yzx + -r4.xyz;
  r2.x = dot(r0.xyz, r0.xyz);
  r2.x = rsqrt(r2.x);
  r0.xyz = r2.xxx * r0.xyz;
  r1.xyz = r0.www ? -r0.xyz : r0.xyz;
  r0.xyz = r0.www ? -r3.xyz : r3.xyz;
  r2.xyz = r1.yzx * r0.xyz;
  r0.xyz = r1.xyz * r0.yzx + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = cmp(r1.w < 0);
  o3.xyzw = r1.xyzw;
  r0.xyz = r0.www ? -r0.xyz : r0.xyz;
  r2.xyz = r0.xyz * r1.yzx;
  r0.xyz = r0.zxy * r1.zxy + -r2.xyz;
  r1.x = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r1.x);
  r0.xyz = r1.xxx * r0.xyz;
  o2.xyz = r0.www ? -r0.xyz : r0.xyz;
  o2.w = 0;
  o4.xyzw = float4(0,0,0,0);
  r0.xyz = cb1[1].xyz * v0.yyy;
  r0.xyz = cb1[0].xyz * v0.xxx + r0.xyz;
  r0.xyz = cb1[2].xyz * v0.zzz + r0.xyz;
  r0.xyz = cb1[3].xyz + r0.xyz;
  r0.xyz = cb0[62].xyz + r0.xyz;
  r1.xyzw = cb0[1].xyzw * r0.yyyy;
  r1.xyzw = r0.xxxx * cb0[0].xyzw + r1.xyzw;
  r0.xyzw = r0.zzzz * cb0[2].xyzw + r1.xyzw;
  o5.xyzw = cb0[3].xyzw + r0.xyzw;
  o6.x = 0;
  return;
}