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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  r0.x = 1 / cb1[7].x;
  r1.xy = v1.xy / v0.ww;
  r0.yzw = v2.xyz / v0.www;
  r3.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r1.w = r3.x * cb1[7].z + cb1[7].w;
  r0.yzw = r0.yzw / r1.www;
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
  r2.xyz = cb0[15].xyz * r0.zzz;
  r2.xyz = cb0[14].xyz * r0.yyy + r2.xyz;
  r0.yzw = cb0[16].xyz * r0.www + r2.xyz;
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
  r0.z = cb0[13].x + cb0[12].x;
  r0.z = 0.5 * r0.z;
  r4.xzw = cb1[6].xxy * float3(0.5,0.5,0.5) + float3(-1,-1,-1);
  r5.x = cb0[12].x;
  r5.y = cb0[13].x;
  r5.xy = min(r5.xy, r4.xx);
  r6.zw = float2(0,0);
  r2.w = 0.0416666679;
  r7.zw = float2(0,0);
  r8.xy = float2(0,0);
  r9.xyz = r3.xyz;
  r9.w = 0;
  int4 r0i;
  r0i.w = 0;
  while (true) {
    if (r0i.w >= 24) break;
    r10.xy = r9.xy;
    r11.zw = min(r10.xy, r4.zw);
    r6.xy = (int2)r11.zw;
    r12.xyzw = t6.Load(r6.xyz).xyzw;
    r1.w = 1 / r12.x;
    r1.w = -cb1[7].y + r1.w;
    r10.z = r1.w * r0.x;
    r1.w = -r1.w * r0.x + r9.z;
    r3.w = cmp(0 < r1.w);
    r1.w = cmp(r1.w < r0.y);
    r1.w = r1.w ? r3.w : 0;
    if (r1.w != 0) {
      r8.xy = r11.zw;
      break;
    }
    if (r1.w != 0) {
      r8.xy = r11.zw;
      break;
    } else {
      r12.xyz = cmp(cb0[12].xyz < r10.xyz);
      r13.xyz = cmp(r10.xyz < cb0[13].xyz);
      r12.xyz = r12.xyz ? r13.xyz : 0;
      r1.w = r12.y ? r12.x : 0;
      r1.w = r12.z ? r1.w : 0;
      r1.w = r1.w ? r3.w : 0;
      if (r1.w != 0) {
        r1.w = cmp(r9.x < r0.z);
        r11.y = r1.w ? r5.x : r5.y;
        r7.xy = (int2)r11.yw;
        r12.xyzw = t6.Load(r7.xyz).xyzw;
        r1.w = 1 / r12.x;
        r1.w = -cb1[7].y + r1.w;
        r1.w = -r1.w * r0.x + r9.z;
        r3.w = cmp(0 < r1.w);
        r1.w = cmp(r1.w < r0.y);
        r1.w = r1.w ? r3.w : 0;
        if (r1.w != 0) {
          r8.xy = r11.yw;
          break;
        }
        r11.x = r11.y;
      } else {
        r11.x = r11.z;
      }
    }
    r9.xy = r10.xy;
    r9.xyzw = r9.xyzw + r2.xyzw;
    r0i.w++;
    r8.xy = r11.xw;
  }
  r0.xy = r8.xy + r8.xy;
  int3 pixelPos = int3(r0.xy, 0);
  r3.xyzw = t2.Load(pixelPos).xyzw;
  r0.y = 0.629960537 + r3.w;
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y + 0.842509866;
  r4.xzw = r3.xyz * r0.y;
  r2.xyzw = t4.Load(pixelPos).xyzw;
  r0.z = cb0[8].w * r2.w;
  r0.z = cmp(r0.z == 0.333333);
  r0.z = r0.z ? 1.000000 : 0;
  r2.xyz = -r3.xyz * r0.y + cb0[8].xyz;
  r0.yzw = r0.z * r2.xyz + r4.xzw;
  r1.w = -1 + cb1[6].z;
  r0.x = dot(r0.xx, r1.ww);
  r0.x = -1 + r0.x;
  r0.x = saturate(max(abs(r0.x), r9.w));
  r0.x = r0.x * r0.x;
  r0.x = cb0[7].w * r0.x;
  r1.w = r4.y * r4.y;
  r1.w = r1.w * -4 + 1;
  r1.w = r5.w * r1.w;
  r1.z = 1 + r1.z;
  r1.z = log2(abs(r1.z));
  r1.z = cb0[10].x * r1.z;
  r1.z = exp2(r1.z);
  r1.z = r1.w * r1.z;
  r2.w = cb0[6].w * r1.z;
  r3.xyz = cb0[7].xyz + -r0.yzw;
  r0.xyz = r0.xxx * r3.xyz + r0.yzw;
  r0.xyz = cb0[6].xyz * r0.xyz;
  r2.xyz = r0.xyz * r2.www;
  r0.xy = cb1[1].xx + r1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.100000001) + r2.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}