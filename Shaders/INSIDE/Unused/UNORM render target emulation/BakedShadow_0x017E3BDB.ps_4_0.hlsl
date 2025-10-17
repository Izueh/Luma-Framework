Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[13];
}

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  nointerpolation float3 v3 : TEXCOORD2,
  nointerpolation float3 v4 : TEXCOORD3,
  nointerpolation float2 v5 : TEXCOORD4,
  nointerpolation float4 v6 : TEXCOORD5,
  nointerpolation float3 v7 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xyz = v2.xyz / v0.w;
  r1.xy = v1.xy / v0.w;
  r2.xy = v0.xy * cb1[6].zw + v0.zz;
  r3.xy = (int2)v0.xy;
  r3.zw = float2(0,0);
  r4.xyzw = t1.Load(r3.xyw).xyzw;
  r0.w = cb1[7].z * r4.x + cb1[7].w;
  r0.w = 1 / r0.w;
  r0.xyz = r0.w * r0.xyz + v3.xyz;
  r5.xyzw = t0.SampleLevel(s0_s, r0.xz, 0).xyzw;
  r0.x = (r5.w == 0.0);
  if (r0.x != 0) discard;
  r3.xyzw = t2.Load(r3.xyz).xyzw;
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = dot(v7.xyz, r3.xyz);
  r0.z = (r0.x < 0);
  if (r0.z != 0) discard;
  r1.w = v1.z;
  r3.xyz = r0.w * r1.xyw + v6.xyz;
  r0.z = v6.w + -r0.w;
  r3.xyz = r3.xyz / r0.z;
  r1.z = r4.x;
  r3.xyz = r3.xyz + r1.xyz;
  r0.z = dot(r3.xyz, r3.xyz);
  r0.z = rsqrt(r0.z);
  r3.xyz = r3.xyz * r0.z;
  r3.xyz = v4.xyz * r3.xyz;
  r3.xyz = r3.xyz / r0.w;
  r2.zw = cb1[1].xx + r1.xy;
  r2.zw = float2(5.39870024,5.44210005) * r2.zw;
  r2.zw = frac(r2.zw);
  r4.xy = float2(21.5351009,14.3136997) + r2.zw;
  r0.z = dot(r2.wz, r4.xy);
  r2.zw = r2.zw + r0.zz;
  r0.z = r2.z * r2.w;
  r0.z = 95.4307022 * r0.z;
  r0.z = frac(r0.z);
  r1.w = 0.0500000007 / r0.x;
  r0.z = r1.w + r0.z;
  r1.xy = v0.xy;
  r1.xyz = r3.xyz * r0.z + r1.xyz;
  r0.z = 1 + r1.w;
  r0.z = r3.z * r0.z;
  r4.zw = float2(0,0);
  r5.xyz = r1.xyz;
  int4 r1i;
  r1i.w = 0;
  while (true) {
    if (r1i.w >= 4) break;
    r4.xy = (int2)r5.xy;
    r6.xyzw = t1.Load(r4.xyz).xyzw;
    r2.z = -r6.x + r5.z;
    r2.w = (0 < r2.z);
    r2.z = (r2.z < r0.z);
    r2.z = r2.z ? r2.w : 0;
    if (r2.z != 0) {
      break;
    }
    r5.xyz = r5.xyz + r3.xyz;
    r1i.w++;
  }
  r0.z = r1i.w;
  r0.z = r0.z * -0.25 + 1;
  r0.y = r0.y * r0.y;
  r0.y = r0.y * -4 + 1;
  r0.y = r5.w * r0.y;
  r0.x = saturate(8 * r0.x);
  r0.x = r0.y * r0.x;
  r0.x = cb0[12].y * r0.x;
  r1.w = r0.z * r0.x;
  r0.x = r0.w * v5.x + v5.y;
  r0.x = max(0, r0.x);
  r0.x = min(cb2[0].w, r0.x);
  r1.xyz = cb2[0].xyz * r0.xxx;
  r0.xy = cb1[1].xx + r2.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}