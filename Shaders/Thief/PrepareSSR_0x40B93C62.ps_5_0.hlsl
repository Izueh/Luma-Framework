Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

#define cmp

void main(
  float2 v0 : TEXCOORD0,
  float3 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  int4 r0i, r5i;
  r0.xyzw = t0.SampleLevel(s2_s, v0.xy, 0).xyzw; // Note: w might go beyond 1 with upgraded textures, but it should be fine
  r1.x = cmp(abs(r0.w) < 9.99999997e-007);
  r0.w = abs(r0.w) * abs(r0.w);
  r1.z = r1.x ? 0 : r0.w;
  r0.w = cmp(r1.z >= 0.00999999978);
  r2.x = cmp(0 < r0.z);
  r0.w = asfloat(asint(r0.w) & asint(r2.x));
  if (r0.w != 0) {
    r2.xy = r0.xy + r0.xy;
    r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
    r0.x = dot(r0.xy, r0.xy);
    r0.x = 1 + -r0.x;
    r2.z = sqrt(r0.x);
    r0.x = t1.SampleLevel(s1_s, v0.xy, 0).x;
    r0.y = 1 + -cb2[2].y;
    r0.x = r0.x + -r0.y;
    r0.x = min(-9.99999996e-013, r0.x);
    r0.x = -cb2[2].x / r0.x;
    r3.xyz = v1.xyz * r0.xxx;
    r4.xyz = v1.xyz * r0.xxx + cb0[5].xyz;
    r2.xyz = float3(-1,-1,-1) + r2.xyz;
    r2.xyz = cb0[11].yyy * r2.xyz + float3(0,0,1);
    r0.x = dot(r3.xyz, r3.xyz);
    r0.x = rsqrt(r0.x);
    r3.xyz = r3.xyz * r0.xxx;
    r0.x = dot(r3.xyz, r2.xyz);
    r0.x = r0.x + r0.x;
    r2.xyz = r2.xyz * -r0.xxx + r3.xyz;
    r0.x = -r2.z * r2.z + 1;
    r0.x = sqrt(r0.x);
    r0.x = r0.z + -r0.x;
    r0.w = dot(r2.xyz, r3.xyz);
    r2.w = cmp(r0.w >= 0);
    r3.x = cmp(0 < r0.x);
    r2.w = asfloat(asint(r2.w) & asint(r3.x));
    if (r2.w != 0) {
      r2.w = 180 / r2.z;
      r3.xyz = cb0[8].xyw * r4.yyy;
      r3.xyz = cb0[7].xyw * r4.xxx + r3.xyz;
      r3.xyz = cb0[9].xyw * r4.zzz + r3.xyz;
      r3.xyz = cb0[10].xyw + r3.xyz;
      r5.xy = r3.xy / r3.zz;
      r3.w = -0.00144269504 * r3.z;
      r3.w = exp2(r3.w);
      r3.w = 30 * r3.w;
      r3.w = trunc(r3.w);
      r3.w = max(1, r3.w);
      r2.w = r2.w / r3.w;
      r6.xyz = r2.xyz * r2.www;
      r2.xyz = r2.xyz * r2.www + r4.xyz;
      r4.xyz = cb0[8].xyw * r2.yyy;
      r2.xyw = cb0[7].xyw * r2.xxx + r4.xyz;
      r2.xyz = cb0[9].xyw * r2.zzz + r2.xyw;
      r2.xyz = cb0[10].xyw + r2.xyz;
      r4.xyz = r2.xyz + -r3.xyz;
      r2.w = r4.z * r3.w;
      r7.xyz = r3.xyz;
      r8.xyz = r2.xyz;
      r9.xy = float2(0,0);
      r4.w = 1;
      r5i.z = 0;
      while (true) {
        r5.w = r5i.z;
        r5.w = cmp(r5.w >= r3.w);
        if (r5.w != 0) break;
        r10.xyz = r7.xyz + r4.xyz;
        r9.zw = r10.xy / r10.zz;
        r5.w = t1.SampleLevel(s1_s, r9.zw, 0).x;
        r5.w = r5.w + -r0.y;
        r5.w = min(-9.99999996e-013, r5.w);
        r5.w = -cb2[2].x / r5.w;
        r5.w = r5.w + -r10.z;
        r6.w = cmp(0 >= r5.w);
        if (r6.w != 0) {
          r8.xyz = r10.xyz;
          r9.xy = r9.zw;
          r4.w = r5.w;
          break;
        }
        r5i.z = r5i.z + 1;
        r7.xyz = r10.xyz;
        r8.xyz = r10.xyz;
        r9.xy = r9.zw;
        r4.w = r5.w;
      }
      r2.x = r5i.z;
      r2.z = cmp(r4.w < 0);
      if (r2.z != 0) {
        r4.xyz = r8.xyz + r7.xyz;
        r10.xyz = float3(0.5,0.5,0.5) * r4.xyz;
        r3.xy = r10.xy / r10.zz;
        r2.z = t1.SampleLevel(s1_s, r3.xy, 0).x;
        r2.z = r2.z + -r0.y;
        r2.z = min(-9.99999996e-013, r2.z);
        r2.z = -cb2[2].x / r2.z;
        r2.z = -r4.z * 0.5 + r2.z;
        r2.z = saturate(100000000 * r2.z);
        r11.xyz = r4.xyz * float3(0.5,0.5,0.5) + -r7.xyz;
        r7.xyz = r2.zzz * r11.xyz + r7.xyz;
        r4.xyz = -r4.xyz * float3(0.5,0.5,0.5) + r8.xyz;
        r4.xyz = r2.zzz * r4.xyz + r10.xyz;
        r8.xyz = r7.xyz + r4.xyz;
        r10.xyz = float3(0.5,0.5,0.5) * r8.xyz;
        r3.xy = r10.xy / r10.zz;
        r3.x = t1.SampleLevel(s1_s, r3.xy, 0).x;
        r3.x = r3.x + -r0.y;
        r3.x = min(-9.99999996e-013, r3.x);
        r3.x = -cb2[2].x / r3.x;
        r3.x = -r8.z * 0.5 + r3.x;
        r3.x = saturate(100000000 * r3.x);
        r11.xyz = r8.xyz * float3(0.5,0.5,0.5) + -r7.xyz;
        r7.xyz = r3.xxx * r11.xyz + r7.xyz;
        r4.xyz = -r8.xyz * float3(0.5,0.5,0.5) + r4.xyz;
        r4.xyz = r3.xxx * r4.xyz + r10.xyz;
        r3.x = 0.25 * r3.x;
        r2.z = r2.z * 0.5 + r3.x;
        r8.xyz = r7.xyz + r4.xyz;
        r10.xyz = float3(0.5,0.5,0.5) * r8.xyz;
        r3.xy = r10.xy / r10.zz;
        r3.x = t1.SampleLevel(s1_s, r3.xy, 0).x;
        r3.x = r3.x + -r0.y;
        r3.x = min(-9.99999996e-013, r3.x);
        r3.x = -cb2[2].x / r3.x;
        r3.x = -r8.z * 0.5 + r3.x;
        r3.x = saturate(100000000 * r3.x);
        r11.xyz = r8.xyz * float3(0.5,0.5,0.5) + -r7.xyz;
        r7.xyz = r3.xxx * r11.xyz + r7.xyz;
        r4.xyz = -r8.xyz * float3(0.5,0.5,0.5) + r4.xyz;
        r4.xyz = r3.xxx * r4.xyz + r10.xyz;
        r2.z = r3.x * 0.125 + r2.z;
        r8.xyz = r7.xyz + r4.xyz;
        r10.xyz = float3(0.5,0.5,0.5) * r8.xyz;
        r3.xy = r10.xy / r10.zz;
        r3.x = t1.SampleLevel(s1_s, r3.xy, 0).x;
        r3.x = r3.x + -r0.y;
        r3.x = min(-9.99999996e-013, r3.x);
        r3.x = -cb2[2].x / r3.x;
        r3.x = -r8.z * 0.5 + r3.x;
        r3.x = saturate(100000000 * r3.x);
        r11.xyz = r8.xyz * float3(0.5,0.5,0.5) + -r7.xyz;
        r7.xyz = r3.xxx * r11.xyz + r7.xyz;
        r4.xyz = -r8.xyz * float3(0.5,0.5,0.5) + r4.xyz;
        r4.xyz = r3.xxx * r4.xyz + r10.xyz;
        r2.z = r3.x * 0.0625 + r2.z;
        r8.xyz = r7.xyz + r4.xyz;
        r10.xyz = float3(0.5,0.5,0.5) * r8.xyz;
        r3.xy = r10.xy / r10.zz;
        r3.x = t1.SampleLevel(s1_s, r3.xy, 0).x;
        r3.x = r3.x + -r0.y;
        r3.x = min(-9.99999996e-013, r3.x);
        r3.x = -cb2[2].x / r3.x;
        r3.x = -r8.z * 0.5 + r3.x;
        r3.x = saturate(100000000 * r3.x);
        r11.xyz = r8.xyz * float3(0.5,0.5,0.5) + -r7.xyz;
        r7.xyz = r3.xxx * r11.xyz + r7.xyz;
        r4.xyz = -r8.xyz * float3(0.5,0.5,0.5) + r4.xyz;
        r4.xyz = r3.xxx * r4.xyz + r10.xyz;
        r2.z = r3.x * 0.03125 + r2.z;
        r4.xyz = r7.xyz + r4.xyz;
        r4.xyw = float3(0.5,0.5,0.5) * r4.zxy;
        r9.xy = r4.yw / r4.xx;
        r3.x = t1.SampleLevel(s1_s, r9.xy, 0).x;
        r0.y = r3.x + -r0.y;
        r0.y = min(-9.99999996e-013, r0.y);
        r0.y = -cb2[2].x / r0.y;
        r0.y = -r4.z * 0.5 + r0.y;
        r3.x = saturate(100000000 * r0.y);
        r2.y = r3.x * 0.015625 + r2.z;
        r0.y = r0.y * r3.w;
        r0.y = 0.00999999978 * r0.y;
        r0.y = min(1, abs(r0.y));
        r0.y = 1 + -r0.y;
      } else {
        r0.y = 0;
        r2.y = 0;
        r4.x = 0;
      }
      r3.xy = r9.xy + -r5.xy;
      r2.z = dot(r3.xy, r3.xy);
      r7.x = sqrt(r2.z);
      r3.xy = min(r9.xy, r5.xy);
      r4.yz = max(r9.xy, r5.xy);
      r4.yz = float2(1,1) + -r4.yz;
      r3.xy = cb0[6].xy * r3.xy;
      r4.yz = cb0[6].xy * r4.yz;
      r3.xy = saturate(float2(0.0199999996,0.0199999996) * r3.xy);
      r4.yz = saturate(float2(0.0199999996,0.0199999996) * r4.yz);
      r2.z = r4.y * r3.x;
      r2.z = r2.z * r3.y;
      r2.z = r2.z * r4.z;
      r3.x = r4.x + -r3.z;
      r2.w = r3.x / r2.w;
      r0.w = saturate(10 * r0.w);
      r0.w = r2.z * r0.w;
      r2.z = cmp(abs(r2.w) < 9.99999997e-007);
      r2.w = abs(r2.w) * abs(r2.w);
      r2.w = r2.w * r2.w;
      r2.w = -r2.w * r2.w + 1;
      r2.z = r2.z ? 1 : r2.w;
      r0.w = r2.z * r0.w;
      r0.y = r0.y * r0.w;
      r0.w = r0.z * r0.z;
      r0.x = r0.w * r0.x;
      r0.x = saturate(500 * r0.x);
      r0.w = cmp(abs(r0.z) < 9.99999997e-007);
      r0.z = log2(abs(r0.z));
      r0.z = 10 * r0.z;
      r0.z = exp2(r0.z);
      r0.x = r0.x * r0.z;
      r0.x = r0.w ? 0 : r0.x;
      r0.x = r0.y * r0.x;
    } else {
      r6.xyz = float3(100,100,100);
      r9.xy = float2(0,0);
      r2.xy = float2(1000000,0);
      r0.x = 0;
      r7.x = 0;
    }
  } else {
    r6.xyz = float3(100,100,100);
    r9.xy = float2(0,0);
    r2.xy = float2(1000000,0);
    r0.x = 0;
    r7.x = 0;
  }
  r0.y = r2.x + r2.y;
  r0.z = dot(r6.xyz, r6.xyz);
  r0.z = sqrt(r0.z);
  r0.y = r0.y * r0.z;

  r0i.y = (int)r0.y;
  r0i.z = r0i.y & 0x80000000;
  r0i.w = max(-r0i.y, r0i.y); // Makes no sense...
  r0i.z = r0i.z ? -(r0i.w & 255) : (r0i.w & 255);

  r0i.y = r0i.y ^ 256; // xor r0i.y, r0i.y, l(256)
  r0i.w = asuint(r0i.w) >> 8; // ushr r0i.w, r0i.w, l(8)
  r0i.y = r0i.y & 0x80000000;
  r0i.y = r0i.y ? -r0i.w : r0i.w;

  r0.w = t2.Sample(s3_s, v0.xy).w;
  r2.x = 1 + -r0.w;
  r0.w = cb0[11].z * r2.x + r0.w;
  r2.w = r0.x * r0.w;
  r0.x = cmp(0.5 < cb0[11].w);
  r0.yz = r0i.yz;

  r1.xy = float2(0.00392156886,0.00392156886) * r0.zy;
  r1.w = 1;
  r7.yz = r1.zw;
  o1.xyzw = r0.x ? r1.xyzw : r7.xxyz;
  r0.xyz = t3.Sample(s0_s, r9.xy).xyz;
#if 1 // Luma: fix negative values
  r0.xyz = max(r0.xyz, 0.0);
#endif
  r2.xyz = r0.xyz * r2.w;
  o0.xyzw = cb0[6].z * r2.xyzw;
#if 1 // Luma: emulate UNORM RTs and blends (this seems to fix some nans glitch in SSR)
  o0.xyzw = saturate(o0.xyzw);
  o1.xyzw = saturate(o1.xyzw);
#endif
}