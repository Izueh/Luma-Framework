Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[29];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  nointerpolation float v6 : TEXCOORD5,
  float3 v7 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyzw = t0.Sample(s3_s, v2.xy).xyzw;
  r1.xyzw = t0.Sample(s3_s, v2.zw).xyzw;
  r0.xyzw = r1.wxwx + r0.wxwx;
  r0.xyzw = r0.xyzw * float4(2,2,2,2) + float4(-2,-2,-2,-2);
  r1.x = 1 + cb0[28].x;
  r1.x = v4.w * r1.x;
  r0.xyzw = r1.xxxx * r0.xyzw;
  r0.xyzw = cb0[26].wzwz * r0.xzyw;
  r1.x = dot(r0.yw, r0.yw);
  r1.x = min(1, r1.x);
  r1.x = 1 + -r1.x;
  r1.x = sqrt(r1.x);
  r1.y = v5.w * r1.x;
  r1.xz = r0.yw;
  r0.w = dot(v4.xyz, v4.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v4.xyz * r0.www;
  r0.w = dot(r1.xyz, -r2.xyz);
  r0.w = 1 + r0.w;
  r0.w = log2(r0.w);
  r0.w = cb0[26].y * r0.w;
  r0.w = exp2(r0.w);
  r0.w = min(1, r0.w);
  r1.x = 1 + -cb2[0].w;
  r1.x = max(v3.z, r1.x);
  r1.x = min(1, r1.x);
  r0.w = r1.x * r0.w;
  r1.xy = v1.xy / v1.ww;
  r3.xyzw = t1.SampleLevel(s1_s, r1.xy, 0).xyzw;
  r1.xy = cb1[1].xx + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.z = cb1[5].z * r3.x + -v1.z;
  r1.z = saturate(cb0[24].x * r1.z);
  r0.w = r1.z * r0.w;
  r1.z = saturate(-cb1[5].y + v1.z);
  r0.w = r1.z * r0.w;
  r1.z = dot(v7.xyz, v7.xyz);
  r3.xyzw = t3.Sample(s0_s, r1.zz).xyzw;
  r1.z = 0.5 * r3.x;
  r0.w = r1.z * r0.w;
  r0.w = cb0[23].w * r0.w;
  r3.xyzw = t2.Sample(s2_s, v3.xy).xyzw;
  r0.w = r3.w * r0.w;
  r0.w = v6.x * r0.w;
  r0.w = 8 * r0.w;
  r3.xyz = cb0[23].xyz * cb0[6].xyz;
  r3.xyz = r3.xyz * r0.www;
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v5.xyz * r0.www + r2.xyz;
  r4.xyz = v5.xyz * r0.www;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r0.w = dot(r0.xz, r0.xz);
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r0.w = sqrt(r0.w);
  r0.y = v5.w * r0.w;
  r0.w = saturate(dot(r0.xyz, r2.xyz));
  r0.x = saturate(dot(r0.xyz, r4.xyz));
  r0.y = log2(r0.w);
  r0.z = cb0[27].x * cb0[27].x;
  r0.z = 8192 * r0.z;
  r0.y = r0.z * r0.y;
  r0.y = exp2(r0.y);
  r0.z = r0.y * r0.y;
  r0.z = r0.z * r0.y;
  r0.w = r0.y * 6 + -15;
  r0.y = r0.y * r0.w + 10;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * cb0[27].y + r0.y;
  r0.yzw = r0.yyy * r3.xyz;
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r1.xxx;
  r1.xyz = frac(r1.xyz);
  r1.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  o0.xyz = r3.xyz * r0.xxx + -r1.xyz;
  r0.xyz = cb0[3].xyz * r0.yzw;
  r0.xz = r0.xx + r0.yz;
  r0.y = r0.y * r0.z;
  r0.x = r0.w * cb0[3].z + r0.x;
  r0.y = sqrt(r0.y);
  r0.y = dot(cb0[3].ww, r0.yy);
  r0.x = r0.x + r0.y;
  o0.w = cb0[27].z * r0.x;

  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.w = saturate(o0.w);
}