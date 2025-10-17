Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

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

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.yzw = v1.xyz / v0.www;
  r1.x = r0.x * r0.y + cb2[8].w;
  r1.y = r0.x * r0.z + cb2[9].w;
  r1.z = r0.x * r0.w + cb2[10].w;
  r0.xyz = cmp(float3(0.5,0.5,0.5) < abs(r1.xyz));
  r1.xyz = float3(0.5,0.5,0.5) + r1.xyz;
  r0.x = asfloat(asint(r0.y) | asint(r0.x));
  r0.x = asfloat(asint(r0.z) | asint(r0.x));
  if (r0.x != 0) discard;
  r0.xyzw = t0.SampleLevel(s0_s, r1.xz, 0).xyzw;
  r2.xyz = cb0[3].xyz * r0.xyz;
  r1.xz = r2.xx + r2.yz;
  r0.w = r2.y * r1.z;
  r1.x = r0.z * cb0[3].z + r1.x;
  r0.xyz = -cb0[13].xyz + r0.xyz;
  r0.w = sqrt(r0.w);
  r0.w = dot(cb0[3].ww, r0.ww);
  r0.w = r1.x + r0.w;
  r0.xyz = r0.www * r0.xyz + cb0[13].xyz;
  r1.w = 0.5;
  r1.xyzw = t1.SampleLevel(s1_s, r1.yw, 0).xyzw;
  r0.w = v1.w * r1.x;
  r0.xyz = r0.xyz * r0.www + -r0.www;
  r0.xyz = float3(1,1,1) + r0.xyz;
  r1.xy = v0.xy * cb1[6].zw + v0.zz;
  r1.xy = cb1[1].xx + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r0.w = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r0.ww;
  r0.w = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.www;
  r1.xyz = frac(r1.xyz);
  r1.xyz = r1.xyz / cb0[12].xxx;
  o0.xyz = r1.xyz + r0.xyz;
  o0.w = 1;

  // Luma: typical UNORM like clamping
  o0 = saturate(o0);
}