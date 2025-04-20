// ---- Created with 3Dmigoto v1.4.1 on Sat Apr 19 22:18:17 2025
Texture2DArray<float4> t9 : register(t9);
Texture3D<float4> t8 : register(t8);
Texture3D<float4> t7 : register(t7);
Texture3D<float4> t6 : register(t6);
TextureCube<float4> t5 : register(t5);
Buffer<uint4> t4 : register(t4);
Buffer<uint4> t3 : register(t3);
Buffer<uint4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);
RWTexture3D<float4> u0 : register(u0);
cbuffer cb3 : register(b3){
  float4 cb3[41];
}
cbuffer cb2 : register(b2){
  float4 cb2[5];
}
cbuffer cb1 : register(b1){
  float4 cb1[188];
}
cbuffer cb0 : register(b0){
  float4 cb0[107];
}

// 3Dmigoto declarations
#define cmp -

[numthreads(4, 4, 4)]
void main(uint3 vThreadID: SV_DispatchThreadID) {
  const float4 icb[] = { { 1.000000, 0, 0, 0},
                              { 0, 1.000000, 0, 0},
                              { 0, 0, 1.000000, 0},
                              { 0, 0, 0, 1.000000} };
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture3d (float,float,float,float) u0
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17;
  uint4 bitmask, uiDest;
  float4 fDest;

// Needs manual fix for instruction:
// unknown dcl_: dcl_thread_group 4, 4, 4
  r0.xyz = (uint3)vThreadID.xyz;
  r0.w = cmp(cb1[27].w < 1);
  r1.x = cmp(asint(cb3[0].z) != 0);
  r1.y = cmp(0 < cb3[6].w);
  r1.x = r1.y ? r1.x : 0;
  r1.y = cmp(0 < cb0[102].x);
  r1.z = cmp(0 < cb0[106].x);
  r1.w = cmp(0 < asuint(cb3[8].x));
  r1.z = r1.w ? r1.z : 0;
  r2.xy = r0.xy * cb2[4].xy + float2(0.5,0.5);
  t9.GetDimensions(0, fDest.x, fDest.y, fDest.z, fDest.w);
  r2.zw = fDest.xy;
  r3.xy = rcp(r2.zw);
  r2.xy = (uint2)r2.xy;
  r1.w = asint(cb3[1].z) + -1;
  r1.w = (int)r1.w;
  r2.xy = (uint2)r2.xy >> asuint(cb3[2].xx);
  r4.xyz = float3(0.5,0.5,1) + r0.xyz;
  r3.zw = cb1[185].xy * r4.xy;
  r3.zw = r3.zw * float2(2,2) + float2(-1,-1);
  r3.zw = float2(1,-1) * r3.zw;
  r4.x = cb2[3].z * r0.z;
  r4.x = exp2(r4.x);
  r4.x = cb2[3].y + r4.x;
  r4.y = cb2[3].x * r4.x;
  r4.x = r4.x * cb2[3].x + cb1[57].w;
  r4.x = cb1[57].z * r4.x;
  r4.x = rcp(r4.x);
  r4.y = r4.y * cb1[26].z + cb1[27].z;
  r4.x = r0.w ? r4.x : r4.y;
  r5.xyzw = cb1[33].xyzw * r3.wwww;
  r5.xyzw = r3.zzzz * cb1[32].xyzw + r5.xyzw;
  r6.xyzw = r4.xxxx * cb1[34].xyzw + r5.xyzw;
  r6.xyzw = cb1[35].xyzw + r6.xyzw;
  r4.x = cb2[3].z * r4.z;
  r4.x = exp2(r4.x);
  r4.x = cb2[3].y + r4.x;
  r4.y = cb2[3].x * r4.x;
  r4.x = r4.x * cb2[3].x + cb1[57].w;
  r4.x = cb1[57].z * r4.x;
  r4.x = rcp(r4.x);
  r4.y = r4.y * cb1[26].z + cb1[27].z;
  r4.x = r0.w ? r4.x : r4.y;
  r4.xyzw = r4.xxxx * cb1[34].xyzw + r5.xyzw;
  r4.xyzw = cb1[35].xyzw + r4.xyzw;
  r5.xyz = r6.xyz / r6.www;
  r4.xyz = r4.xyz / r4.www;
  r4.xyz = r5.xyz + -r4.xyz;
  r4.x = dot(r4.xyz, r4.xyz);
  r4.x = sqrt(r4.x);
  r5.xyz = vThreadID.xyz;
  r5.w = 0;
  r6.xyzw = t8.Load(r5.xyzw).xyzw;
  r4.x = max(1, r4.x);
  r4.y = 1 + -r6.w;
  r4.z = cmp(0 < r4.y);
  r7.xyzw = float4(0,0,0,0);
  while (true) {
    r4.w = cmp((uint)r7.w >= 2);
    if (r4.w != 0) break;
    r8.xyz = cb0[r7.w+78].xyz + r0.xyz;
    r8.xy = cb1[185].xy * r8.xy;
    r8.xy = r8.xy * float2(2,2) + float2(-1,-1);
    r8.xy = float2(1,-1) * r8.xy;
    r4.w = cb2[3].z * r8.z;
    r4.w = exp2(r4.w);
    r4.w = cb2[3].y + r4.w;
    r6.w = cb2[3].x * r4.w;
    r4.w = r4.w * cb2[3].x + cb1[57].w;
    r4.w = cb1[57].z * r4.w;
    r4.w = rcp(r4.w);
    r8.z = r6.w * cb1[26].z + cb1[27].z;
    r4.w = r0.w ? r4.w : r8.z;
    r9.xyzw = cb0[71].xyzw * r8.yyyy;
    r8.xyzw = r8.xxxx * cb0[70].xyzw + r9.xyzw;
    r8.xyzw = r4.wwww * cb0[72].xyzw + r8.xyzw;
    r8.xyzw = cb0[73].xyzw + r8.xyzw;
    r8.xyz = r8.xyz / r8.www;
    r8.xyz = -cb1[62].xyz + r8.xyz;
    r9.xyz = cb1[59].xyz + -r8.xyz;
    r4.w = dot(r9.xyz, r9.xyz);
    r4.w = rsqrt(r4.w);
    r9.xyz = r9.xyz * r4.www;
    if (r1.x != 0) {
      if (cb3[34].y != 0) {
        r10.xyzw = cb3[37].xyzw * r8.yyyy;
        r10.xyzw = r8.xxxx * cb3[36].xyzw + r10.xyzw;
        r10.xyzw = r8.zzzz * cb3[38].xyzw + r10.xyzw;
        r10.xyzw = cb3[39].xyzw + r10.xyzw;
        r10.xy = r10.xy / r10.ww;
        r11.xy = saturate(r10.xy);
        r11.xy = cmp(r10.xy == r11.xy);
        r4.w = r11.y ? r11.x : 0;
        if (r4.w != 0) {
          r10.xy = cb3[35].xy * r10.xy;
          r10.xy = cb3[35].zw * r10.xy;
          r4.w = t0.SampleLevel(s0_s, r10.xy, 0).x;
          r8.w = r4.w + -r10.z;
          r8.w = saturate(r8.w * 40 + 1);
          r4.w = cmp(0.99000001 < r4.w);
          r4.w = r4.w ? 1.000000 : 0;
          r4.w = r8.w + r4.w;
          r4.w = min(1, r4.w);
        } else {
          r4.w = 1;
        }
      } else {
        r4.w = 1;
      }
      if (r1.z != 0) {
        r8.w = cb3[8].x;
        r9.w = 0;
        while (true) {
          r10.x = cmp((uint)r9.w >= asuint(cb3[8].x));
          if (r10.x != 0) break;
          r10.x = dot(cb3[9].xyzw, icb[r9.w+0].xyzw);
          r10.x = cmp(r6.w < r10.x);
          if (r10.x != 0) {
            r8.w = r9.w;
            break;
          }
          r9.w = (int)r9.w + 1;
        }
        r9.w = cmp((uint)r8.w < asuint(cb3[8].x));
        if (r9.w != 0) {
          r9.w = (uint)r8.w << 2;
          r10.xyzw = cb3[r9.w+11].xyzw * r8.yyyy;
          r10.xyzw = r8.xxxx * cb3[r9.w+10].xyzw + r10.xyzw;
          r10.xyzw = r8.zzzz * cb3[r9.w+12].xyzw + r10.xyzw;
          r10.xyzw = cb3[r9.w+13].xyzw + r10.xyzw;
          r10.xy = r10.xy / r10.ww;
          r11.xy = cmp(r10.xy >= cb3[r8.w+26].xy);
          r11.zw = cmp(cb3[r8.w+26].zw >= r10.xy);
          r11.xy = r11.zw ? r11.xy : 0;
          r9.w = r11.y ? r11.x : 0;
          if (r9.w != 0) {
            r10.xy = r10.xy * r2.zw;
            r11.xy = r10.xy * r3.xy;
            r11.z = cb3[r8.w+30].x;
            r9.w = t9.SampleLevel(s4_s, r11.xyz, 0).x;
            r10.x = r9.w + -r10.z;
            r10.x = saturate(r10.x * cb3[40].x + 1);
            r9.w = cmp(0.99000001 < r9.w);
            r9.w = r9.w ? 1.000000 : 0;
            r9.w = r10.x + r9.w;
            r4.w = min(1, r9.w);
          }
        }
      }
      r9.w = dot(cb3[5].xyz, r9.xyz);
      r10.x = -r9.w * -1.20000005 + 1.36000001;
      r10.y = cmp(r10.x < 9.99999997e-07);
      r10.x = log2(abs(r10.x));
      r10.x = -1.5 * r10.x;
      r10.x = exp2(r10.x);
      r10.x = r10.y ? 0 : r10.x;
      r9.w = r9.w * r9.w + 1;
      r9.w = r10.x * r9.w;
      r9.w = 0.0323704928 * r9.w;
      r10.xyz = cb3[6].xyz * r9.www;
      r7.xyz = r10.xyz * r4.www + r7.xyz;
    }
    if (r1.y != 0) {
      r10.xyz = t5.SampleLevel(s2_s, r9.xyz, 0).xyz;
      r10.xyz = float3(0.282094806,0.282094806,0.282094806) * r10.xyz;
      r10.xyz = max(float3(0,0,0), r10.xyz);
      r7.xyz = r10.xyz + r7.xyz;
    }
    r4.w = r6.w * cb3[3].x + cb3[3].y;
    r4.w = log2(r4.w);
    r4.w = cb3[3].z * r4.w;
    r4.w = max(0, r4.w);
    r4.w = min(r4.w, r1.w);
    r4.w = (uint)r4.w;
    r4.w = mad((int)r4.w, asint(cb3[1].y), (int)r2.y);
    r4.w = mad((int)r4.w, asint(cb3[1].x), (int)r2.x);
    r6.w = (uint)r4.w << 1;
    r6.w = t2.Load(r6.w).x;
    bitmask.w = ((~(-1 << 31)) << 1) & 0xffffffff;  r4.w = (((uint)r4.w << 1) & bitmask.w) | ((uint)1 & ~bitmask.w);
    r4.w = t2.Load(r4.w).x;
    r10.xyz = r7.xyz;
    r10.w = 0;
    while (true) {
      r9.w = cmp((uint)r10.w >= (uint)r6.w);
      if (r9.w != 0) break;
      r9.w = (int)r4.w + (int)r10.w;
      r9.w = t3.Load(r9.w).x;
      r11.x = (int)r9.w * 11;
      r11.y = mad((int)r9.w, 11, 3);
      r12.xyzw = t4.Load(r11.y).xyzw;
      r11.y = (uint)r12.w >> 16;
      r11.y = f16tof32(r11.y);
      r11.y = cmp(0 < r11.y);
      if (r11.y != 0) {
        r11.xyzw = t4.Load(r11.x).xyzw;
        r13.xyz = mad((int3)r9.www, int3(11,11,11), int3(1,2,5));
        r14.xyzw = t4.Load(r13.x).xyzw;
        r13.xyw = t4.Load(r13.y).xyz;
        r13.z = t4.Load(r13.z).y;
        r15.x = cmp(-2 < r12.x);
        r11.xyz = r11.xyz + -r8.xyz;
        r15.y = dot(r11.xyz, r11.xyz);
        r15.z = rsqrt(r15.y);
        r16.xyz = r15.zzz * r11.xyz;
        r15.z = dot(r16.xyz, -r13.xyw);
        r15.w = cmp(r15.z < 0);
        r15.z = min(1, abs(r15.z));
        r16.x = r15.z * -0.0187292993 + 0.0742610022;
        r16.x = r16.x * r15.z + -0.212114394;
        r16.x = r16.x * r15.z + 1.57072878;
        r15.z = 1 + -r15.z;
        r15.z = sqrt(r15.z);
        r15.z = r16.x * r15.z;
        r16.x = r15.w ? -1 : 1;
        r15.w = r15.w ? 3.141593 : 0;
        r15.z = r15.z * r16.x + r15.w;
        r16.x = -r15.z * 0.318309873 + 1;
        r13.z = (uint)r13.z;
        r13.z = 0.5 + r13.z;
        r16.y = cb0[0].w * r13.z;
        r13.z = t1.SampleLevel(s1_s, r16.xy, 0).x;
        r14.xyz = r14.xyz * r13.zzz;
        if (r14.w != 0) {
          r13.z = 1;
        } else {
          r12.w = f16tof32(r12.w);
          r9.w = mad((int)r9.w, 11, 4);
          r16.xyzw = t4.Load(r9.w).xyzw;
          r9.w = cmp(0 < (uint)r16.w);
          r11.w = r11.w * r11.w;
          r11.w = r15.y * r11.w;
          r11.w = -r11.w * r11.w + 1;
          r11.w = max(0, r11.w);
          r11.w = r11.w * r11.w;
          r14.w = cmp(0 < r12.w);
          r9.w = r9.w ? r14.w : 0;
          if (r9.w != 0) {
            r9.w = 0.5 * r12.w;
            r17.xyz = r9.www * r16.xyz + r11.xyz;
            r16.xyz = -r9.www * r16.xyz + r11.xyz;
            r9.w = dot(r17.xyz, r17.xyz);
            r9.w = sqrt(r9.w);
            r12.w = dot(r16.xyz, r16.xyz);
            r12.w = sqrt(r12.w);
            r17.xyz = r12.www * r17.xyz;
            r16.xyz = r9.www * r16.xyz + r17.xyz;
            r9.w = r12.w + r9.w;
            r9.w = max(9.99999975e-06, r9.w);
            r11.xyz = r16.xyz / r9.www;
            r15.y = dot(r11.xyz, r11.xyz);
          }
          r9.w = r4.x * r4.x + r15.y;
          r9.w = r12.z * r12.z + r9.w;
          r9.w = rcp(r9.w);
          r13.z = r11.w * r9.w;
        }
        r9.w = dot(r11.xyz, r11.xyz);
        r9.w = rsqrt(r9.w);
        r11.xyz = r11.xyz * r9.www;
        r9.w = dot(r11.xyz, r13.xyw);
        r9.w = r9.w + -r12.x;
        r9.w = saturate(r9.w * r12.y);
        r9.w = r9.w * r9.w;
        r9.w = r13.z * r9.w;
        r9.w = r15.x ? r9.w : r13.z;
        r11.x = dot(r11.xyz, r9.xyz);
        r11.y = -r11.x * -1.20000005 + 1.36000001;
        r11.z = cmp(r11.y < 9.99999997e-07);
        r11.y = log2(abs(r11.y));
        r11.y = -1.5 * r11.y;
        r11.y = exp2(r11.y);
        r11.y = r11.z ? 0 : r11.y;
        r11.x = r11.x * r11.x + 1;
        r11.x = r11.y * r11.x;
        r11.x = 0.0323704928 * r11.x;
        r11.yzw = r14.xyz * r9.www;
        r10.xyz = r11.yzw * r11.xxx + r10.xyz;
      }
      r10.w = (int)r10.w + 1;
    }
    if (r4.z != 0) {
      r8.xyz = t5.SampleLevel(s2_s, r9.xyz, 0).xyz;
      r10.xyz = r8.xyz * r4.yyy + r10.xyz;
    }
    r7.xyz = r6.xyz * cb1[128].yyy + r10.xyz;
    r7.w = (int)r7.w + 1;
  }

  // Replace with direct output (no temporal accumulation)
  r1.xyz = r7.xyz;
  r1.w = 1.0;

  r2.x = cmp(0 < cb0[80].w);
  if (r2.x != 0) {
    r2.xyzw = float4(0.5,0.5,0.5,0.5) + r0.zxyz;
    r0.x = cb2[3].z * r2.x;
    r0.x = exp2(r0.x);
    r0.x = cb2[3].y + r0.x;
    r0.y = cb2[3].x * r0.x;
    r0.x = r0.x * cb2[3].x + cb1[57].w;
    r0.x = cb1[57].z * r0.x;
    r0.x = rcp(r0.x);
    r0.y = r0.y * cb1[26].z + cb1[27].z;
    r0.x = r0.w ? r0.x : r0.y;
    r4.xyzw = cb0[71].xyzw * r3.wwww;
    r3.xyzw = r3.zzzz * cb0[70].xyzw + r4.xyzw;
    r0.xyzw = r0.xxxx * cb0[72].xyzw + r3.xyzw;
    r0.xyzw = cb0[73].xyzw + r0.xyzw;
    r0.xyz = r0.xyz / r0.www;
    r0.xyz = -cb1[62].xyz + r0.xyz;
    r3.xyz = cb0[75].xyw * r0.yyy;
    r0.xyw = r0.xxx * cb0[74].xyw + r3.xyz;
    r0.xyz = r0.zzz * cb0[76].xyw + r0.xyw;
    r0.xyz = cb0[77].xyw + r0.xyz;
    r0.w = rcp(r0.z);
    r0.xy = r0.xy * r0.ww;
    r3.xy = r0.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r0.x = r0.z * cb1[187].x + cb1[187].y;
    r0.x = log2(r0.x);
    r0.x = cb1[187].z * r0.x;
    r3.z = cb1[185].z * r0.x;
    r0.xyz = saturate(r3.xyz);
    r0.xyz = cmp(r3.xyz != r0.xyz);
    r0.x = (int)r0.y | (int)r0.x;
    r0.x = (int)r0.z | (int)r0.x;
    r0.yzw = cb1[185].xyz * r2.yzw;
    r0.xyz = r0.xxx ? r0.yzw : r3.xyz;
    r2.xy = cb1[184].xy * r0.xy;
    r2.zw = float2(-0.5,-0.5) + cb1[184].xy;
    r2.xy = max(float2(0.5,0.5), r2.xy);
    r2.xy = min(r2.xy, r2.zw);
    r0.xy = cb1[186].xy * r2.xy;
    r0.xyzw = t7.SampleLevel(s3_s, r0.xyz, 0).xyzw;
    r2.xyz = min(float3(65504,65504,65504), r0.xyz);
    r0.xyz = cb0[97].yyy * r2.xyz;

    // Replace with direct assignment
    r1.xyzw = r0.xyzw;
  }
  r0.xyz = cmp((uint3)vThreadID.xyz < asuint(cb2[0].xyz));
  r0.x = r0.y ? r0.x : 0;
  r0.x = r0.z ? r0.x : 0;
  if (r0.x != 0) {
    r1.xyz = cb1[128].xxx * r1.xyz;
  // No code for instruction (needs manual fix):
    //store_uav_typed u0.xyzw, vThreadID.xyzz, r1.xyzw
	u0[vThreadID.xyz] = r1.xyzw;
  }
  return;
}