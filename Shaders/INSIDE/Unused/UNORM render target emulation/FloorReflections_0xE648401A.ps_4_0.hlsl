Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[3];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[11];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[17];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  nointerpolation float3 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  r0.x = 1 / cb1[7].x;
  r1.xy = v1.xy / v0.ww;
  r0.yzw = v2.xyz / v0.w;
  r3.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r1.w = r3.x * cb1[7].z + cb1[7].w;
  r0.yzw = r0.yzw / r1.w;
  r4.x = cb2[8].w + r0.y;
  r4.y = cb2[9].w + r0.z;
  r4.z = cb2[10].w + r0.w;
  r0.yz = float2(0.5,0.5) + r4.xz;
  r5.xyzw = t0.SampleLevel(s0_s, r0.yz, 0).xyzw;
  r0.yzw = cmp(float3(0.5,0.5,0.5) < abs(r4.xyz));
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  r0.z = cmp(r5.w < 0.00392156886);
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  if (r0.y != 0) discard;
  r2.xyzw = t5.Load(int3(v0.xy, 0)).xyzw;
  r0.yzw = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = cb0[15].xyz * r0.z;
  r2.xyz = cb0[14].xyz * r0.y + r2.xyz;
  r0.yzw = cb0[16].xyz * r0.w + r2.xyz;
  r1.z = 1;
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  r2.xyz = r2.xxx * r1.xyz;
  r1.z = dot(r0.yzw, r2.xyz);
  r2.w = dot(r1.zz, cb0[10].yy);
  r0.yzw = -r0.yzw * r2.www + r2.xyz;
  r2.xy = -r1.xy * r0.ww + r0.yz;
  r2.z = r0.w * r1.w;
  r2.xyz = v3.xyz * r2.xyz;
  r0.y = v3.z * r1.w;
  r0.zw = float2(2803.7793,2867.22314) * cb3[2].xy;
  r0.zw = v0.xy * cb0[9].xy + r0.zw;
  r6.xyzw = t1.SampleLevel(s1_s, r0.zw, 0).xyzw;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.w = 0.5 * r0.z;

  int r0iz = asint(r0.z) >> 1;
  r0iz = 0x5f375a86 - r0iz;
  r0.z = asfloat(r0iz);

  r1.w = r0.z * r0.z;
  r0.w = -r0.w * r1.w + 1.5;
  r0.z = dot(r0.zz, r0.ww);
  r0.z = r6.w + r0.z;
  r5.xy = float2(0.5,0.5) * v0.xy;
  r5.z = r3.x;
  r3.xyz = r2.xyz * r0.zzz + r5.xyz;
  r0.zw = cb1[6].xy * float2(0.5,0.5) + float2(-1,-1);
  r1.w = r0.y + r0.y;
  r6.zw = float2(0,0);
  r2.w = 0.0416666679;
  r7.xyz = r3.xyz;
  r7.w = 0;
  r3.w = 0;
  r4.xzw = float3(0,0,0);
  r5.xyz = float3(0,0,0);
  r8.xyzw = float4(0,0,0,0);
  int4 r8i = 0;
  while (true) {
    if (r8i.w >= 24) break;
    r9.xy = min(r7.xy, r0.zw);
    r6.xy = (int2)r9.xy;
    r10.xyzw = t6.Load(r6.xyz).xyzw;
    r6.x = 1 / r10.x;
    r6.x = -cb1[7].y + r6.x;
    r6.y = r6.x * r0.x;
    r9.z = -r6.x * r0.x + r7.z;
    r9.w = cmp(0 < r9.z);
    r10.x = cmp(r9.z < r0.y);
    r10.y = r9.w ? r10.x : 0;
    if (r10.y != 0) {
      r4.xz = r9.xy;
      r3.w = r9.z;
      break;
    }
    if (r9.w != 0) {
      if (r10.x != 0) {
        r4.xz = r9.xy;
        r3.w = r9.z;
        break;
      }
      r6.x = -r6.x * r0.x + r8.y;
      r6.x = cmp(r1.w < abs(r6.x));
      r6.x = r6.x ? r4.w : 0;
      if (r6.x != 0) {
        r4.xz = r9.xy;
        r3.w = r9.z;
        r5.z = -1;
        break;
      }
    } else {
      r5.xy = r9.xy;
      r8.x = -r9.z;
      r8.z = r7.w;
    }
    r7.xyzw = r7.xyzw + r2.xyzw;
    r4.w = r9.w;
    r8.y = r6.y;
    r8i.w++;
    r4.xz = r9.xy;
    r3.w = r9.z;
    r5.z = 0;
  }
  r0.xy = r4.xz + r4.xz;
  if (r5.z != 0) {
    r0.zw = r5.xy + r5.xy;
    r1.w = r8.x + -r3.w;
    r2.x = max(r8.x, r3.w);
    r1.w = r1.w / r2.x;
    r1.w = r1.w * 0.5 + 0.5;
    r2.xyzw = t2.Load(int3(r0.zw, 0)).xyzw;
    r3.xyzw = t2.Load(int3(r0.xy, 0)).xyzw;
    r3.xyzw = r3.xyzw + -r2.xyzw;
    r2.xyzw = r1.wwww * r3.xyzw + r2.xyzw;
    r0.z = 0.629960537 + r2.w;
    r0.z = r0.z * r0.z;
    r0.z = r0.z * r0.z + 0.842509866;
    r2.xyz = r2.xyz * r0.zzz;
    r0.z = -r8.z + r7.w;
    r7.w = r1.w * r0.z + r8.z;
  } else {
    int3 pixelCoord = int3(r0.xy, 0);
    r6.xyzw = t2.Load(pixelCoord).xyzw;
    r0.y = 0.629960537 + r6.w;
    r0.y = r0.y * r0.y;
    r0.y = r0.y * r0.y + 0.842509866;
    r4.xzw = r6.xyz * r0.yyy;
    r3.xyzw = t4.Load(pixelCoord).xyzw;
    r0.z = cb0[8].w * r3.w;
    r0.z = cmp(r0.z == 0.333333);
    r0.z = r0.z ? 1.000000 : 0;
    r3.xyz = -r6.xyz * r0.yyy + cb0[8].xyz;
    r2.xyz = r0.zzz * r3.xyz + r4.xzw;
  }
  r0.y = -1 + cb1[6].z;
  r0.x = dot(r0.xx, r0.yy);
  r0.x = -1 + r0.x;
  r0.x = saturate(max(abs(r0.x), r7.w));
  r0.x = r0.x * r0.x;
  r0.x = cb0[7].w * r0.x;
  r0.y = r4.y * r4.y;
  r0.y = r0.y * -4 + 1;
  r0.y = r5.w * r0.y;
  r0.z = 1 + r1.z;
  r0.z = log2(abs(r0.z));
  r0.z = cb0[10].x * r0.z;
  r0.z = exp2(r0.z);
  r0.y = r0.y * r0.z;
  r3.w = cb0[6].w * r0.y;
  r0.yzw = cb0[7].xyz + -r2.xyz;
  r0.xyz = r0.xxx * r0.yzw + r2.xyz;
  r0.xyz = cb0[6].xyz * r0.xyz;
  r3.xyz = r0.xyz * r3.www;
  r0.xy = cb1[1].xx + r1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.100000001) + r3.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}