Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[20];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float w2 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = cmp(w2.x < 0);
  r1.xyzw = t0.Sample(s1_s, v2.xy).xyzw;
  r0.y = cmp(r1.w < 0.00392156886);
  r0.x = asfloat(asint(r0.x) | asint(r0.y));
  if (r0.x != 0) discard;
  r0.xy = v1.xy / v1.ww;
  r0.xyzw = t1.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r0.x = cb1[5].z * r0.x + -v1.z;
  r0.x = saturate(cb0[11].x * r0.x);
  r0.x = cb0[10].w * r0.x;
  r0.y = saturate(-cb1[5].y + v1.z);
  r0.x = r0.x * r0.y;
  r0.y = saturate(w2.x);
  r0.z = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * r0.y;
  r0.w = r0.x * r1.w;
  r0.xyz = cb0[10].xyz * r0.www;
  r1.xy = cb0[19].zz + v1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r2.w = 1 / cb0[11].y;
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886);
  o0.xyzw = -r1.xyzw * r2.xyzw + r0.xyzw;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}