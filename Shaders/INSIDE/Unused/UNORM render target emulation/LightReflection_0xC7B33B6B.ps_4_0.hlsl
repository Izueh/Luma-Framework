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

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  nointerpolation float3 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
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
  r0.yzw = (float3(0.5,0.5,0.5) < abs(r4.xyz));
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  r0.z = (r5.w < 0.00392156886);
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
  r0.y = dot(r0.yzw, r2.xyz);
  r1.z = dot(r0.yy, cb0[10].yy);
  r0.zw = -r0.zw * r1.zz + r2.yz;
  r2.y = -r1.y * r0.w + r0.z;
  r2.z = r0.w * r1.w;
  r2.yz = v3.yz * r2.yz;
  r0.z = v3.z * r1.w;
  r1.zw = float2(2803.7793,2867.22314) * cb3[2].xy;
  r1.zw = v0.xy * cb0[9].xy + r1.zw;
  r6.xyzw = t1.SampleLevel(s1_s, r1.zw, 0).xyzw;
  r0.w = dot(r2.yz, r2.yz);
  r1.z = 0.5 * r0.w;
  
  int r0iw = asint(r0.w) >> 1;
  r0iw = 0x5f375a86 - r0iw;
  r0.w = asfloat(r0iw);

  r1.w = r0.w * r0.w;
  r1.z = -r1.z * r1.w + 1.5;
  r0.w = dot(r0.ww, r1.zz);
  r0.w = r6.w + r0.w;
  r2.xw = float2(0,0.0416666679);
  r5.xy = float2(0.5,0.5) * v0.xy;
  r5.z = r3.x;
  r3.xyz = r2.xyz * r0.www + r5.xyz;
  r1.zw = cb1[6].xy * float2(0.5,0.5) + float2(-1,-1);
  r4.xz = float2(0,0);
  r7.xyz = r3.xyz;
  r7.w = 0;
  int4 r0i;
  r0i.w = 0;
  while (true) {
    if (r0i.w >= 24) break;
    r5.xy = min(r7.xy, r1.zw);
    r8.xyzw = t6.Load(int3(r5.xy, 0)).xyzw;
    r3.w = 1 / r8.x;
    r3.w = -cb1[7].y + r3.w;
    r3.w = -r3.w * r0.x + r7.z;
    r4.w = (0 < r3.w);
    r3.w = (r3.w < r0.z);
    r3.w = r3.w ? r4.w : 0;
    if (r3.w != 0) {
      r4.xz = r5.xy;
      break;
    }
    r7.xyzw = r7.xyzw + r2.xyzw;
    r0i.w++;
    r4.xz = r5.xy;
  }
  r0.xz = r4.xz + r4.xz;
  int3 sampelPixel = int3(r0.xz, 0);
  r3.xyzw = t2.Load(sampelPixel).xyzw;
  r0.x = 0.629960537 + r3.w;
  r0.x = r0.x * r0.x;
  r0.x = r0.x * r0.x + 0.842509866;
  r4.xzw = r3.xyz * r0.x;
  r2.xyzw = t4.Load(sampelPixel).xyzw;
  r0.z = cb0[8].w * r2.w;
  r0.z = (r0.z == 0.333333);
  r0.z = r0.z ? 1.000000 : 0;
  r2.xyz = -r3.xyz * r0.x + cb0[8].xyz;
  r0.xzw = r0.zzz * r2.xyz + r4.xzw;
  r1.z = r7.w * r7.w;
  r1.z = cb0[7].w * r1.z;
  r1.w = r4.y * r4.y;
  r1.w = r1.w * -4 + 1;
  r1.w = r5.w * r1.w;
  r0.y = 1 + r0.y;
  r0.y = log2(abs(r0.y));
  r0.y = cb0[10].x * r0.y;
  r0.y = exp2(r0.y);
  r0.y = r1.w * r0.y;
  r2.w = cb0[6].w * r0.y;
  r3.xyz = cb0[7].xyz + -r0.xzw;
  r0.xyz = r1.zzz * r3.xyz + r0.xzw;
  r0.xyz = cb0[6].xyz * r0.xyz;
  r2.xyz = r0.xyz * r2.w;
  r0.xy = cb1[1].xx + r1.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.x;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.100000001) + r2.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}