Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[24];
}

void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD2,
  float4 v2 : TEXCOORD8,
  float2 v3 : TEXCOORD9,
  uint v4 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2;
  r0.xy = v1.xy * float2(2,-2) + float2(-1,1);
  r0.xy = r0.xy * cb2[0].xy + cb2[0].wz;
  r1.xyz = t0.Sample(s1_s, r0.xy).xyz;
  r2.xyz = t1.Sample(s2_s, r0.xy).xyz;
  r0.xyz = t2.Sample(s0_s, r0.xy).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.w = dot(r0.xyz, float3(0.333000004,0.333000004,0.333000004));
  r2.xyz = r0.www + -r0.xyz;
  r0.xyz = cb0[12].zzz * r2.xyz + r0.xyz;
  r0.w = -cb0[12].z * 0.5 + 1;
  r0.xyz = r0.xyz * r0.www + r1.xyz;
  o0.xyz = cb0[23].xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}