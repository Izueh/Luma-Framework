Texture2D<float4> t0 : register(t0);

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
  float4 cb0[10];
}

void main(
  float4 v0 : SV_POSITION0,
  nointerpolation float4 v1 : TEXCOORD0,
  float1 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyzw = t0.Load(r0.xyz).xyzw;
  r0.x = cb1[7].z * r0.x + cb1[7].w;
  r0.x = 1 / r0.x;
  r0.y = min(v2.x, r0.x);
  r0.y = r0.y + -r0.x;
  r0.zw = saturate(v3.zx / abs(v3.wy));
  r0.x = r0.z * r0.y + r0.x;
  r0.xy = r0.xx * v1.xz + v1.yw;
  r0.y = max(0, r0.y);
  r0.x = saturate(r0.x);
  r0.x = r0.x * r0.w;
  r1.w = cb0[8].w * r0.x;
  r0.x = min(cb2[0].w, r0.y);
  r0.x = cb0[7].w * r0.x;
  r0.yzw = cb2[0].xyz + -cb0[8].xyz;
  r0.xyz = r0.xxx * r0.yzw + cb0[8].xyz;
  r1.xyz = r0.xyz * r1.www;
  r0.xy = v0.xy * cb1[6].zw + v0.zz;
  r0.xy = cb1[1].xx + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r2.xyzw = frac(r2.xyzw);
  r0.xyzw = r2.xyzw + r0.xyzw;
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r2.w = cb0[9].z * r0.w;
  o0.xyzw = -r2.xyzw + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}