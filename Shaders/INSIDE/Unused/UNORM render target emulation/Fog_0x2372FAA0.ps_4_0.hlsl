Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[1];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[16];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[17];
}

// Purely adds to the scene based on distance
// Doesn't seem raise blacks close to the camera
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float w2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = 0.628122985 + cb2[15].x;
  r0.yz = cb0[8].zz + v3.xy;
  r0.xy = r0.yz + r0.xx;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.yzw = float3(95.4307022,97.5901031,93.8368988) * r0.xxx;
  r1.xyz = float3(75.0490875,75.0495682,75.0496063) * r0.xxx;
  r1.xyz = frac(r1.xyz);
  r0.xyz = frac(r0.yzw);
  r0.xyz = r0.xyz + r1.xyz;
  r0.xyz = float3(-0.5,-0.5,-0.5) + r0.xyz;
  r0.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r1.xy = v3.xy / v3.ww;
  r1.xyzw = t0.Sample(s1_s, r1.xy).xyzw;
  r0.w = cb1[7].z * r1.x + cb1[7].w;
  r0.w = 1 / r0.w;
  r0.w = -v3.z + r0.w;
  r0.w = saturate(cb0[16].x * r0.w);
  r1.w = v1.w * r0.w;
  r1.xyz = v1.xyz;
  r1.xyzw = r1.xyzw + r1.xyzw;
  r1.xyzw = cb0[6].xyzw * r1.xyzw;
  r2.xyzw = t1.Sample(s0_s, v2.xy).xyzw;
  r1.xyzw = r2.xyzw * r1.xyzw;
  r0.w = 1 - cb3[0].w;
  r0.w = max(w2.x, r0.w);
  r0.w = min(1, r0.w);
  r0.w = r1.w * r0.w;
  r0.xyz = r1.xyz * r0.w - r0.xyz;
  r0.w = dot(r0.xyz, float3(0.333000004,0.333000004,0.333000004));
  o0.xyz = r0.xyz;
  o0.w = cb0[7].x * r0.w;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.w = saturate(o0.w);
}