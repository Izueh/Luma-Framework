// ---- Created with 3Dmigoto v1.4.1 on Sat Apr 19 18:50:24 2025
Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture3D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[140];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[28];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v2.xy;
  r1.xy = trunc(v2.xy);
  r1.xy = float2(0.5,0.5) + r1.xy;
  r1.xy = -cb1[121].xy + r1.xy;
  r1.xy = cb1[122].zw * r1.xy;
  r1.xy = r1.xy * float2(2,2) + float2(-1,-1);
  r1.zw = float2(1,-1) * r1.xy;
  r0.z = asuint(cb1[139].z) << 3;
  r2.xyz = (int3)r0.xyz & int3(63,63,63);
  r2.w = 0;
  r0.z = t0.Load(r2.xyzw).x;
  r2.xyzw = (int4)cb1[121].xyxy;
  r3.xyzw = cb1[122].xyxy + cb1[121].xyxy;
  r3.xyzw = float4(-1,-1,-1,-1) + r3.xyzw;
  r3.xyzw = (int4)r3.xyzw;
  r4.xy = max((int2)r2.zw, (int2)r0.xy);
  r4.xy = min((int2)r4.xy, (int2)r3.xy);
  r4.zw = float2(0,0);
  r5.x = t1.Load(r4.xyw).x;
  r6.xyzw = (int4)r0.xyxy + int4(-2,-2,2,-2);
  r6.xyzw = max((int4)r6.xyzw, (int4)r2.xyzw);
  r6.xyzw = min((int4)r6.zwxy, (int4)r3.zwxy);
  r7.xy = r6.zw;
  r7.zw = float2(0,0);
  r7.x = t1.Load(r7.xyz).x;
  r6.zw = float2(0,0);
  r7.y = t1.Load(r6.xyz).x;
  r6.xyzw = (int4)r0.xyxy + int4(-2,2,2,2);
  r6.xyzw = max((int4)r6.xyzw, (int4)r2.xyzw);
  r6.xyzw = min((int4)r6.zwxy, (int4)r3.zwxy);
  r8.xy = r6.zw;
  r8.zw = float2(0,0);
  r7.z = t1.Load(r8.xyz).x;
  r6.zw = float2(0,0);
  r7.w = t1.Load(r6.xyz).x;
  r0.w = max(r7.y, r7.z);
  r0.w = max(r7.x, r0.w);
  r6.x = max(r0.w, r7.w);
  r0.w = cmp(r5.x < r6.x);
  r7.xyzw = cmp(r6.xxxx == r7.xyzw);
  r8.xyz = r7.xzy ? float3(0,0,0) : 0;
  bitmask.x = ((~(-1 << 1)) << 1) & 0xffffffff;  r7.x = (((uint)r7.y << 1) & bitmask.x) | ((uint)0 & ~bitmask.x);
  bitmask.y = ((~(-1 << 1)) << 1) & 0xffffffff;  r7.y = (((uint)r7.w << 1) & bitmask.y) | ((uint)0 & ~bitmask.y);
  bitmask.z = ((~(-1 << 1)) << 1) & 0xffffffff;  r7.z = (((uint)r7.z << 1) & bitmask.z) | ((uint)0 & ~bitmask.z);
  r5.w = (int)r7.x + (int)r8.x;
  r5.w = (int)r8.y + (int)r5.w;
  r9.y = (int)r7.y + (int)r5.w;
  r5.w = (int)r8.z + (int)r8.x;
  r5.w = (int)r7.z + (int)r5.w;
  r9.z = (int)r7.y + (int)r5.w;
  r7.xy = max(int2(-2,-2), (int2)r9.yz);
  r6.yz = min(int2(0,0), (int2)r7.xy);
  r5.yz = float2(0,0);
  r5.xyz = r0.www ? r6.xyz : r5.xyz;
  r6.xyz = cb1[115].xyw * r1.www;
  r6.xyz = r1.zzz * cb1[114].xyw + r6.xyz;
  r6.xyz = r5.xxx * cb1[116].xyw + r6.xyz;
  r6.xyz = cb1[117].xyw + r6.xyz;
  r5.xw = r6.xy / r6.zz;
  r5.xw = r1.xy * float2(1,-1) + -r5.xw;
  r5.yz = (int2)r0.xy + (int2)r5.yz;
  r5.yz = max((int2)r5.yz, (int2)r2.xy);
  r6.xy = min((int2)r5.yz, (int2)r3.xy);
  r6.zw = float2(0,0);
  r5.yz = t4.Load(r6.xyz).xy;
  r0.w = dot(r5.yz, r5.yz);
  r0.w = cmp(0 < r0.w);
  //   float2 jitterDelta = cb1[118].xy - cb1[118].zw;
  // r5.xy += cb1[118].xy;
  r5.yz = float2(-0.499992371,-0.499992371) + r5.yz;
  r5.yz = float2(4.00801611,4.00801611) * r5.yz;
  r5.xy = r0.ww ? r5.yz : r5.xw;


  // o0.xyzw = r5.yzyz;

  
  o0 = r5;
  // o0.xyz = max(float3(0,0,0), r0.xyz);
  // o0.w = (int)r4.x & 0x3f800000;
  return;
}