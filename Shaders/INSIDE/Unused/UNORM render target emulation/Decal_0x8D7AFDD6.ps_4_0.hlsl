Texture2D<float4> t3 : register(t3);
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
  nointerpolation float3 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r1.xyzw = t2.Load(int3(v0.xy, 0)).xyzw;
  r0.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = dot(r0.xyz, v2.xyz);
  r0.x = saturate(-cb0[13].y + r0.x);
  r0.y = cb1[7].z * r1.x + cb1[7].w;
  r0.y = 1 / r0.y;
  r1.xyz = v1.xyz / v0.www;
  r2.x = r0.y * r1.x + cb2[8].w;
  r2.y = r0.y * r1.y + cb2[9].w;
  r2.z = r0.y * r1.z + cb2[10].w;
  r0.yzw = cmp(float3(0.5,0.5,0.5) < abs(r2.xyz));
  r1.xyz = float3(0.5,0.5,0.5) + r2.xyz;
  r0.y = asfloat(asint(r0.z) | asint(r0.y));
  r0.y = asfloat(asint(r0.w) | asint(r0.y));
  if (r0.y != 0) discard;
  r1.w = 0.5;
  r2.xyzw = t1.SampleLevel(s1_s, r1.yw, 0).xyzw;
  r1.xyzw = t0.SampleLevel(s0_s, r1.xz, 0).xyzw;
  r0.x = r2.x * r0.x;
  r0.x = v1.w * r0.x;
  r0.yzw = cb0[3].xyz * r1.xyz;
  r0.yw = r0.yy + r0.zw;
  r0.z = r0.z * r0.w;
  r0.y = r1.z * cb0[3].z + r0.y;
  r1.xyz = -cb0[12].xyz + r1.xyz;
  r0.z = sqrt(r0.z);
  r0.z = r0.z + r0.z;
  r0.y = r0.z * cb0[3].w + r0.y;
  r0.yzw = r0.yyy * r1.xyz + cb0[12].xyz;
  r0.xyz = r0.yzw * -r0.xxx + r0.xxx;
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
  o0.xyz = -r1.xyz * float3(0.00392156886,0.00392156886,0.00392156886) + r0.xyz;
  o0.w = 1;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}