Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

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
  float4 cb0[14];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  nointerpolation float v2 : TEXCOORD1,
  nointerpolation float3 w2 : TEXCOORD2,
  nointerpolation float4 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r1.xyzw = t3.Load(r0.xyw).xyzw;
  r0.xyzw = t4.Load(r0.xyz).xyzw;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = dot(r0.xyz, w2.xyz);
  r0.x = saturate(-cb0[13].y + r0.x);
  r0.y = cb1[7].z * r1.x + cb1[7].w;
  r0.y = 1 / r0.y;
  r1.xyz = v1.xyz / v0.www;
  r2.x = r0.y * r1.x + cb2[8].w;
  r2.y = r0.y * r1.y + cb2[9].w;
  r2.z = r0.y * r1.z + cb2[10].w;
  r0.yzw = (float3(0.5,0.5,0.5) < abs(r2.xyz));
  r1.xyz = float3(0.5,0.5,0.5) + r2.xyz;
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  if (r0.y != 0) discard;
  r2.xyzw = r1.xzxz * cb0[8].xyxy + cb0[8].zwzw;
  r2.xyzw = frac(r2.xyzw);
  r2.xyzw = r2.xyzw / cb0[10].xyxy;
  r2.xyzw = v3.xyzw + r2.xyzw;
  r3.xyzw = t2.SampleLevel(s0_s, r2.zw, cb0[7].w).xyzw;
  r2.xyzw = t2.SampleLevel(s0_s, r2.xy, cb0[7].w).xyzw;
  r0.y = dot(r2.xyz, cb0[7].xyz);
  r0.z = dot(r3.xyz, cb0[7].xyz);
  r0.z = r0.z + -r0.y;
  r0.y = v2.x * r0.z + r0.y;
  r2.xyzw = t1.SampleLevel(s1_s, r1.xz, 0).xyzw;
  r0.y = r2.x * r0.y;
  r0.x = r0.y * r0.x;
  r1.w = 0.5;
  r1.xyzw = t0.SampleLevel(s2_s, r1.yw, 0).xyzw;
  r0.x = r1.x * r0.x;
  r0.yz = v0.xy * cb1[6].zw + v0.zz;
  r0.yz = cb1[1].xx + r0.yz;
  r0.xyz = float3(4,5.39870024,5.44210005) * r0.xyz;
  r0.yz = frac(r0.yz);
  r1.xy = float2(21.5351009,14.3136997) + r0.yz;
  r0.w = dot(r0.zy, r1.xy);
  r0.yz = r0.yz + r0.ww;
  r0.y = r0.y * r0.z;
  r0.yzw = float3(95.4307022,97.5901031,93.8368988) * r0.yyy;
  r0.yzw = frac(r0.yzw);
  r0.yzw = float3(0.00392156886,0.00392156886,0.00392156886) * r0.yzw;
  o0.xyz = cb0[6].xyz * r0.xxx + -r0.yzw;
  o0.w = 0;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
}