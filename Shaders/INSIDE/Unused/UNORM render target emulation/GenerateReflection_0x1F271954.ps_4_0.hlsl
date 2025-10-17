Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb3 : register(b3)
{
  float4 cb3[1];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[17];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float3 v5 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyz = cb0[9].xyz * v3.yyy;
  r0.xyz = cb0[8].xyz * v3.xxx + r0.xyz;
  r0.xyz = cb0[10].xyz * v3.zzz + r0.xyz;
  r0.xyz = cb0[11].xyz + r0.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.xyzw = t0.Sample(s0_s, r0.xx).xyzw;
  r0.xyz = v5.xyz * r0.xxx;
  r1.xyz = cb2[0].xyz + -v3.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.w = saturate(dot(v2.xyz, r1.xyz));
  r0.xyz = r0.xyz * r0.www;
  r0.w = max(0, v4.x);
  r0.w = min(cb3[0].w, r0.w);
  r0.xyz = r0.www * -r0.xyz + r0.xyz;
  r0.w = v4.y / v4.z;
  r0.w = saturate(cb0[15].y + abs(r0.w));
  r1.x = saturate(cb0[15].y + v4.y);
  r0.w = r1.x + -r0.w;
  r0.w = cb0[16].w * r0.w;
  r0.xyz = r0.www * -r0.xyz + r0.xyz;
  r1.xy = cb1[1].xx + v1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r0.w = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r0.ww;
  r0.w = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.www;
  r2.xyz = float3(75.0490875,75.0495682,75.0496063) * r0.www;
  r2.xyz = frac(r2.xyz);
  r1.xyz = frac(r1.xyz);
  r1.xyz = r1.xyz + r2.xyz;
  o0.xyz = -r1.xyz * float3(0.00196078443,0.00196078443,0.00196078443) + r0.xyz;
  o0.w = 1;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
}