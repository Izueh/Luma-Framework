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
  r0.xyz = (float3(0.5,0.5,0.5) < abs(r1.xyz));
  r1.xyz = float3(0.5,0.5,0.5) + r1.xyz;
  r0.x = asfloat(asint(r0.y) | asint(r0.x));
  r0.x = asfloat(asint(r0.z) | asint(r0.x));
  if (r0.x != 0) discard;
  r0.xyzw = t0.SampleLevel(s0_s, r1.xz, 0).xyzw;
  r0.xyw = cb0[3].xyz * r0.xyz;
  r0.xw = r0.xx + r0.yw;
  r0.y = r0.y * r0.w;
  r0.x = r0.z * cb0[3].z + r0.x;
  r0.y = sqrt(r0.y);
  r0.y = dot(cb0[3].ww, r0.yy);
  r0.x = r0.x + r0.y;
  r0.y = 1 + -r0.x;
  r1.w = 0.5;
  r1.xyzw = t1.SampleLevel(s1_s, r1.yw, 0).xyzw;
  r0.z = cb0[12].x * r1.x;
  r0.y = r0.z * r0.y;
  r0.z = v1.w * r0.z;
  r0.x = r0.z * r0.x + -r0.z;
  r1.w = 1 + r0.x;
  r0.x = saturate(cb0[13].w * r0.y);
  r1.xyz = cb0[13].xyz * r0.xxx;
  r0.xy = v0.xy * cb1[6].zw + v0.zz;
  r0.xy = cb1[1].xx + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r2.w = -cb0[12].y * r0.w;
  o0.xyzw = -r2.xyzw + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}