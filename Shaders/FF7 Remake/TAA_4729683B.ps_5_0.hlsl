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
  r8.xyz = r7.xzy ? float3(-nan,-nan,-nan) : 0;
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
  r5.yz = float2(-0.499992371,-0.499992371) + r5.yz;
  r5.yz = float2(4.00801611,4.00801611) * r5.yz;
  r5.xy = r0.ww ? r5.yz : r5.xw;
  r6.xyzw = (int4)r0.xyxy + int4(0,-1,-1,0);
  r6.xyzw = max((int4)r6.xyzw, (int4)r2.xyzw);
  r6.xyzw = min((int4)r6.zwxy, (int4)r3.zwxy);
  r7.xy = r6.zw;
  r7.zw = float2(0,0);
  r8.xyz = t2.Load(r7.xyw).xyz;
  r8.xyz = min(float3(65504,65504,65504), r8.xyz);
  r9.x = dot(r8.xzy, float3(1,1,2));
  r9.y = dot(r8.xz, float2(2,-2));
  r9.z = dot(r8.xzy, float3(-1,-1,2));
  r6.zw = float2(0,0);
  r8.xyz = t2.Load(r6.xyw).xyz;
  r8.xyz = min(float3(65504,65504,65504), r8.xyz);
  r10.x = dot(r8.xzy, float3(1,1,2));
  r10.y = dot(r8.xz, float2(2,-2));
  r10.z = dot(r8.xzy, float3(-1,-1,2));
  r8.xyz = t2.Load(r4.xyw).xyz;
  r8.xyz = min(float3(65504,65504,65504), r8.xyz);
  r11.x = dot(r8.xzy, float3(1,1,2));
  r11.y = dot(r8.xz, float2(2,-2));
  r11.z = dot(r8.xzy, float3(-1,-1,2));
  r8.xyzw = (int4)r0.xyxy + int4(1,0,0,1);
  r2.xyzw = max((int4)r8.xyzw, (int4)r2.xyzw);
  r2.xyzw = min((int4)r2.zwxy, (int4)r3.zwxy);
  r3.xy = r2.zw;
  r3.zw = float2(0,0);
  r0.xyw = t2.Load(r3.xyw).xyz;
  r0.xyw = min(float3(65504,65504,65504), r0.xyw);
  r8.x = dot(r0.xwy, float3(1,1,2));
  r8.y = dot(r0.xw, float2(2,-2));
  r8.z = dot(r0.xwy, float3(-1,-1,2));
  r2.zw = float2(0,0);
  r0.xyw = t2.Load(r2.xyw).xyz;
  r0.xyw = min(float3(65504,65504,65504), r0.xyw);
  r12.x = dot(r0.xwy, float3(1,1,2));
  r12.y = dot(r0.xw, float2(2,-2));
  r12.z = dot(r0.xwy, float3(-1,-1,2));
  r0.x = 4 + r9.x;
  r0.x = rcp(r0.x);
  r0.y = cb0[18].x * r0.x;
  r0.w = 4 + r10.x;
  r0.w = rcp(r0.w);
  r0.w = cb0[19].x * r0.w;
  r13.xyz = r0.www * r10.xyz;
  r13.xyz = r0.yyy * r9.xyz + r13.xyz;
  r0.x = cb0[18].x * r0.x + r0.w;
  r0.y = 4 + r11.x;
  r0.y = rcp(r0.y);
  r0.w = cb0[20].x * r0.y;
  r13.xyz = r0.www * r11.xyz + r13.xyz;
  r0.x = cb0[20].x * r0.y + r0.x;
  r0.y = 4 + r8.x;
  r0.y = rcp(r0.y);
  r0.w = cb0[21].x * r0.y;
  r13.xyz = r0.www * r8.xyz + r13.xyz;
  r0.x = cb0[21].x * r0.y + r0.x;
  r0.y = 4 + r12.x;
  r0.y = rcp(r0.y);
  r0.w = cb0[22].x * r0.y;
  r13.xyz = r0.www * r12.xyz + r13.xyz;
  r0.x = cb0[22].x * r0.y + r0.x;
  r0.x = rcp(r0.x);
  r13.yzw = r13.xyz * r0.xxx;
  r0.yw = r1.xy * float2(1,-1) + -r5.xy;
  r1.x = max(abs(r0.y), abs(r0.w));
  r1.x = cmp(r1.x < 1);
  if (r1.x != 0) {
    r1.xy = r0.wy * cb1[123].yx + cb1[123].zw;
    r1.xy = cb1[126].yx * r1.xy;
    r5.xy = float2(1.5,1.5) + cb1[124].yx;
    r5.zw = cb1[125].yx + cb1[124].yx;
    r5.zw = float2(-1.5,-1.5) + r5.zw;
    r1.xy = max(r5.xy, r1.xy);
    r1.xy = min(r1.xy, r5.zw);
    r1.xy = cb0[1].wz * r1.xy;
    r5.xy = r1.yx * cb0[1].xy + float2(-0.5,-0.5);
    r5.xy = floor(r5.xy);
    r14.xyzw = float4(0.5,0.5,-0.5,-0.5) + r5.xyxy;
    r1.xy = r1.xy * cb0[1].yx + -r14.yx;
    r5.zw = r1.yx * r1.yx;
    r15.xy = r5.zw * r1.yx;
    r15.zw = r5.zw * r1.yx + -r5.zw;
    r15.zw = float2(0.5,0.5) * r15.zw;
    r1.xy = r1.xy * r1.xy + -r1.xy;
    r5.zw = float2(2.5,2.5) * r5.zw;
    r5.zw = r15.xy * float2(1.5,1.5) + -r5.zw;
    r5.xyzw = float4(2.5,2.5,1,1) + r5.xyzw;
    r15.xy = r1.xy * float2(0.5,0.5) + -r15.wz;
    r1.xy = -r1.xy * float2(0.5,0.5) + float2(1,1);
    r16.zw = cb0[1].zw * r14.zw;
    r14.zw = max(float2(9.99999975e-05,9.99999975e-05), r1.yx);
    r5.zw = r5.zw / r14.zw;
    r5.zw = saturate(float2(1,1) + -r5.zw);
    r5.zw = r14.xy + r5.zw;
    r16.xy = cb0[1].zw * r5.zw;
    r5.xy = cb0[1].zw * r5.xy;
    r14.xyzw = t3.SampleLevel(s0_s, r16.xw, 0).xyzw;
    r14.xyz = min(float3(65504,65504,65504), r14.xyz);
    r15.xy = r1.yx * r15.xy;
    r17.xyzw = t3.SampleLevel(s0_s, r16.zy, 0).xyzw;
    r17.xyz = min(float3(65504,65504,65504), r17.xyz);
    r17.xyzw = r17.xyzw * r15.yyyy;
    r14.xyzw = r14.xyzw * r15.xxxx + r17.xyzw;
    r8.w = r15.x + r15.y;
    r17.xyzw = t3.SampleLevel(s0_s, r16.xy, 0).xyzw;
    r17.xyz = min(float3(65504,65504,65504), r17.xyz);
    r9.w = r1.y * r1.x;
    r14.xyzw = r17.xyzw * r9.wwww + r14.xyzw;
    r8.w = r1.y * r1.x + r8.w;
    r5.zw = r16.yx;
    r16.xyzw = t3.SampleLevel(s0_s, r5.xz, 0).xyzw;
    r16.xyz = min(float3(65504,65504,65504), r16.xyz);
    r5.xz = r15.zw * r1.xy;
    r14.xyzw = r16.xyzw * r5.xxxx + r14.xyzw;
    r1.x = r15.z * r1.x + r8.w;
    r16.xyzw = t3.SampleLevel(s0_s, r5.wy, 0).xyzw;
    r16.xyz = min(float3(65504,65504,65504), r16.xyz);
    r5.xyzw = r16.xyzw * r5.zzzz + r14.xyzw;
    r1.x = r15.w * r1.y + r1.x;
    r5.xyzw = r5.xyzw / r1.xxxx;
    r5.xyzw = max(float4(0,0,0,0), r5.xyzw);
    r5.xyz = min(float3(65504,65504,65504), r5.xyz);
    r1.x = cb1[128].x * cb0[27].y;
    r5.xyz = r5.xyz * r1.xxx;
    r1.x = cmp(0 < r5.w);
    r14.x = dot(r5.xzy, float3(1,1,2));
    r14.y = dot(r5.xz, float2(2,-2));
    r14.z = dot(r5.xzy, float3(-1,-1,2));
    r5.xy = t4.Load(r7.xyz).xy;
    r1.y = dot(r5.xy, r5.xy);
    r1.y = cmp(0 < r1.y);
    r5.xy = t4.Load(r6.xyz).xy;
    r5.x = dot(r5.xy, r5.xy);
    r5.x = cmp(0 < r5.x);
    r4.xy = t4.Load(r4.xyz).xy;
    r4.x = dot(r4.xy, r4.xy);
    r4.x = cmp(0 < r4.x);
    r3.xy = t4.Load(r3.xyz).xy;
    r3.x = dot(r3.xy, r3.xy);
    r3.x = cmp(0 < r3.x);
    r2.xy = t4.Load(r2.xyz).xy;
    r2.x = dot(r2.xy, r2.xy);
    r2.x = cmp(0 < r2.x);
    r1.y = (int)r1.y | (int)r5.x;
    r1.y = (int)r4.x | (int)r1.y;
    r1.y = (int)r3.x | (int)r1.y;
    r1.y = (int)r2.x | (int)r1.y;
    r1.y = ~(int)r1.y;
    r1.x = r1.x ? r1.y : 0;
    r2.xyz = r1.xxx ? r13.yzw : r14.xyz;
    r0.yw = cb1[125].xy * r0.yw;
    r0.yw = r1.zw * cb1[122].xy + -r0.yw;
    r0.y = dot(r0.yw, r0.yw);
    r0.y = sqrt(r0.y);
    r1.xyz = min(r11.xyz, r10.xyz);
    r1.xyz = min(r9.xyz, r1.xyz);
    r3.xyz = min(r12.xyz, r8.xyz);
    r1.xyz = min(r3.xyz, r1.xyz);
    r3.xyz = max(r11.xyz, r10.xyz);
    r3.xyz = max(r9.xyz, r3.xyz);
    r4.yzw = max(r12.xyz, r8.xyz);
    r3.xyz = max(r4.yzw, r3.xyz);
    r4.yzw = r10.xyz + r9.xyz;
    r4.yzw = r4.yzw + r11.xyz;
    r4.yzw = r4.yzw + r8.xyz;
    r4.yzw = r4.yzw + r12.xyz;
    r5.xyz = float3(0.200000003,0.200000003,0.200000003) * r4.yzw;
    r6.xyz = r10.xyz * r10.xyz;
    r6.xyz = r9.xyz * r9.xyz + r6.xyz;
    r6.xyz = r11.xyz * r11.xyz + r6.xyz;
    r6.xyz = r8.xyz * r8.xyz + r6.xyz;
    r6.xyz = r12.xyz * r12.xyz + r6.xyz;
    r5.xyz = r5.xyz * r5.xyz;
    r5.xyz = r6.xyz * float3(0.200000003,0.200000003,0.200000003) + -r5.xyz;
    r5.xyz = max(float3(0,0,0), r5.xyz);
    r5.xyz = sqrt(r5.xyz);
    r5.xyz = float3(1.25,1.25,1.25) * r5.xyz;
    r6.xyz = r4.yzw * float3(0.200000003,0.200000003,0.200000003) + -r5.xyz;
    r1.xyz = max(r6.xyz, r1.xyz);
    r4.yzw = r4.yzw * float3(0.200000003,0.200000003,0.200000003) + r5.xyz;
    r3.xyz = min(r4.yzw, r3.xyz);
    r1.xyz = max(r2.xyz, r1.xyz);
    r1.xyz = min(r1.xyz, r3.xyz);
    r0.y = 0.0250000004 * r0.y;
    r0.y = min(1, r0.y);
    r0.y = r0.y * 0.159999996 + 0.0399999991;
    r0.w = 0.00999999978 * r2.x;
    r1.w = r13.x * r0.x + -r2.x;
    r1.w = max(9.99999975e-05, abs(r1.w));
    r0.w = r0.w / r1.w;
    r0.w = min(1, r0.w);
    r0.y = max(r0.y, r0.w);
    r0.x = r13.x * r0.x + 4;
    r0.w = 4 + r2.x;
    r0.xw = rcp(r0.xw);
    r1.w = 1 + -r0.y;
    r2.x = r1.w * r0.w;
    r0.x = r0.y * r0.x;
    r2.yzw = r13.yzw * r0.xxx;
    r1.xyz = r1.xyz * r2.xxx + r2.yzw;
    r0.x = r1.w * r0.w + r0.x;
    r0.x = rcp(r0.x);
    r13.yzw = r1.xyz * r0.xxx;
  } else {
    r4.x = -1;
  }
  r1.x = dot(r13.yzw, float3(0.25,0.25,-0.25));
  r1.y = dot(r13.yw, float2(0.25,0.25));
  r1.z = dot(r13.yzw, float3(0.25,-0.25,-0.25));
  r0.xyw = max(float3(0,0,0), r1.xyz);
  r0.z = r0.z * 2 + -1;
  r1.xyz = r0.xyw * r0.zzz;
  r0.xyz = r1.xyz * float3(0.00048828125,0.00048828125,0.00048828125) + r0.xyw;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = (int)r4.x & 0x3f800000;
  return;
}