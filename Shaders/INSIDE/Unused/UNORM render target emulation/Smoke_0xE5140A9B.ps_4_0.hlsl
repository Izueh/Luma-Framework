Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[21];
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = log2(v1.x);
  r0.x = abs(cb0[18].y) * r0.x;
  r0.x = exp2(r0.x);
  r0.x = v3.w * r0.x;
  r0.yz = v2.xy / v2.ww;
  r1.xyzw = t0.SampleLevel(s0_s, r0.yz, 0).xyzw;
  r0.y = cb1[5].z * r1.x + -v2.z;
  r0.y = saturate(cb0[20].y * r0.y);
  r0.x = r0.x * r0.y;
  r1.xyz = v3.xyz * r0.xxx;
  r1.w = v1.y * r0.x;
  r0.xy = cb1[1].xx + v2.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r2.w = cb0[20].w * r0.w;
  o0.xyzw = -r2.xyzw + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}