Texture2D<float4> t8 : register(t8);
Texture2D<float4> t7 : register(t7);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s8_s : register(s8);
SamplerState s7_s : register(s7);
SamplerState s6_s : register(s6);
SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
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
  float4 cb0[39];
}

#define cmp

// Generates a blue wave like depth layer that loops in distance from the camera, not sure what it's for
void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  float2 w1 : TEXCOORD2,
  float2 v2 : TEXCOORD3,
  float2 w2 : TEXCOORD4,
  float2 v3 : TEXCOORD5,
  float2 w3 : TEXCOORD6,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = t3.Sample(s4_s, v1.xy).y;
  r0.y = t4.Sample(s5_s, w1.xy).y;
  r0.z = t5.Sample(s6_s, v2.xy).y;
  r0.w = 1;
  r0.xyzw = cb0[29].xyzw * r0.xyzw;
  r1.x = t6.Sample(s1_s, v1.xy).y;
  r1.y = t7.Sample(s2_s, w1.xy).y;
  r1.z = t8.Sample(s3_s, v2.xy).y;
  r1.w = 1;
  r1.xyzw = cb0[27].xyzw * r1.xyzw + -r0.xyzw;
  r2.x = cmp(0 < cb0[38].x);
  r2.y = t2.Sample(s8_s, v0.zw).x;
  r2.z = t1.Sample(s7_s, v0.zw).x;
  r2.w = cmp(r2.z < r2.y);
  r2.x = r2.w ? r2.x : 0;
  r2.w = cmp(r2.z != 0.000000);
  r2.x = r2.w ? r2.x : 0;
  r2.y = r2.x ? 5 : r2.y;
  r2.w = cmp(r2.y >= r2.z);
  r3.x = cmp(cb0[38].x == 0.000000);
  r2.w = r2.w ? r3.x : 0;
  r2.x = r2.w ? r2.z : 0;
  r2.z = t0.Sample(s0_s, v0.xy).x;
  r2.w = 1 + -cb2[2].y;
  r2.z = r2.z + -r2.w;
  r2.z = min(-9.99999996e-013, r2.z);
  r2.z = -cb2[2].x / r2.z;
  r2.xy = min(r2.zz, r2.xy);
  r3.xyzw = r2.yyyy * cb0[31].xyzw + cb0[33].xyzw;
  r4.xyzw = r2.xxxx * cb0[31].xyzw + cb0[33].xyzw;
  r3.xyzw = -r4.xyzw + r3.xyzw;
  r4.xyzw = r2.zzzz * cb0[31].xyzw + cb0[33].xyzw;
  r5.xyzw = r2.zzzz * cb0[32].xyzw + cb0[34].xyzw;
  r5.xyzw = max(float4(9.99999997e-007,9.99999997e-007,9.99999997e-007,9.99999997e-007), r5.xyzw);
  r5.xyzw = float4(1,1,1,1) / r5.xyzw;
  r4.xyzw = max(float4(9.99999997e-007,9.99999997e-007,9.99999997e-007,9.99999997e-007), r4.xyzw);
  r4.xyzw = float4(1,1,1,1) / r4.xyzw;
  r3.xyzw = r4.xyzw * r3.xyzw;
  o0.xyzw = r3.xyzw * r1.xyzw + r0.xyzw;
  r0.xyzw = r2.yyyy * cb0[32].xyzw + cb0[34].xyzw;
  r1.xyzw = r2.xxxx * cb0[32].xyzw + cb0[34].xyzw;
  r0.xyzw = -r1.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw * r5.xyzw;
  r1.x = t3.Sample(s4_s, w2.xy).y;
  r1.y = t4.Sample(s5_s, v3.xy).y;
  r1.z = t5.Sample(s6_s, w3.xy).y;
  r1.w = 1;
  r1.xyzw = cb0[30].xyzw * r1.xyzw;
  r2.x = t6.Sample(s1_s, w2.xy).y;
  r2.y = t7.Sample(s2_s, v3.xy).y;
  r2.z = t8.Sample(s3_s, w3.xy).y;
  r2.w = 1;
  r2.xyzw = cb0[28].xyzw * r2.xyzw + -r1.xyzw;
  o1.xyzw = r0.xyzw * r2.xyzw + r1.xyzw;
  
  // Luma: fix UNORM to FLOAT RT upgrades (there's nans otherwise)
  o0.a = saturate(o0.a);
  o1.a = saturate(o1.a);
  o0.rgb = max(o0.rgb, 0);
  o1.rgb = max(o1.rgb, 0);
}