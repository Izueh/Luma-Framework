Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s4_s : register(s4);
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
  float4 v7 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = v7.xy / v7.ww;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.xyzw = t3.Sample(s0_s, r0.xy).xyzw;
  r0.x = (0 < v7.z);
  r0.x = r0.x ? 1.000000 : 0;
  r0.x = r0.x * r0.w;
  r0.y = dot(v7.xyz, v7.xyz);
  r1.xyzw = t4.Sample(s1_s, r0.yy).xyzw;
  r0.x = r1.x * r0.x;
  r0.x = 0.5 * r0.x;
  r1.xyzw = t0.Sample(s4_s, v2.xy).xyzw;
  r2.xyzw = t0.Sample(s4_s, v2.zw).xyzw;
  r1.xyzw = r2.wxwx + r1.wxwx;
  r1.xyzw = r1.xyzw * float4(2,2,2,2) + float4(-2,-2,-2,-2);
  r0.y = 1 + cb0[28].x;
  r0.y = v4.w * r0.y;
  r1.xyzw = r1.xyzw * r0.yyyy;
  r1.xyzw = cb0[26].wzwz * r1.xzyw;
  r0.y = dot(r1.yw, r1.yw);
  r0.y = min(1, r0.y);
  r0.y = 1 + -r0.y;
  r0.y = sqrt(r0.y);
  r2.y = v5.w * r0.y;
  r2.xz = r1.yw;
  r0.y = dot(v4.xyz, v4.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v4.xyz * r0.yyy;
  r1.w = dot(r2.xyz, -r0.yzw);
  r1.w = 1 + r1.w;
  r1.w = log2(r1.w);
  r1.w = cb0[26].y * r1.w;
  r1.w = exp2(r1.w);
  r1.w = min(1, r1.w);
  r2.x = 1 + -cb2[0].w;
  r2.x = max(v3.z, r2.x);
  r2.x = min(1, r2.x);
  r1.w = r2.x * r1.w;
  r2.xy = v1.xy / v1.ww;
  r3.xyzw = t1.SampleLevel(s2_s, r2.xy, 0).xyzw;
  r2.xy = cb1[1].xx + r2.xy;
  r2.xy = float2(5.39870024,5.44210005) * r2.xy;
  r2.xy = frac(r2.xy);
  r2.z = cb1[5].z * r3.x + -v1.z;
  r2.z = saturate(cb0[24].x * r2.z);
  r1.w = r2.z * r1.w;
  r2.z = saturate(-cb1[5].y + v1.z);
  r1.w = r2.z * r1.w;
  r0.x = r1.w * r0.x;
  r0.x = cb0[23].w * r0.x;
  r3.xyzw = t2.Sample(s3_s, v3.xy).xyzw;
  r0.x = r3.w * r0.x;
  r0.x = v6.x * r0.x;
  r0.x = 8 * r0.x;
  r3.xyz = cb0[23].xyz * cb0[6].xyz;
  r3.xyz = r3.xyz * r0.xxx;
  r0.x = dot(v5.xyz, v5.xyz);
  r0.x = rsqrt(r0.x);
  r0.yzw = v5.xyz * r0.xxx + r0.yzw;
  r4.xyz = v5.xyz * r0.xxx;
  r0.x = dot(r0.yzw, r0.yzw);
  r0.x = rsqrt(r0.x);
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = dot(r1.xz, r1.xz);
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r0.w = sqrt(r0.w);
  r1.y = v5.w * r0.w;
  r0.x = saturate(dot(r1.xyz, r0.xyz));
  r0.y = saturate(dot(r1.xyz, r4.xyz));
  r0.x = log2(r0.x);
  r0.z = cb0[27].x * cb0[27].x;
  r0.z = 8192 * r0.z;
  r0.x = r0.z * r0.x;
  r0.x = exp2(r0.x);
  r0.z = r0.x * r0.x;
  r0.z = r0.z * r0.x;
  r0.w = r0.x * 6 + -15;
  r0.x = r0.x * r0.w + 10;
  r0.x = r0.z * r0.x;
  r0.y = r0.y * cb0[27].y + r0.x;
  r0.xzw = r0.xxx * r3.xyz;
  r1.xy = float2(21.5351009,14.3136997) + r2.xy;
  r1.x = dot(r2.yx, r1.xy);
  r1.xy = r2.xy + r1.xx;
  r1.x = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r1.xxx;
  r1.xyz = frac(r1.xyz);
  r1.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r1.xyz;
  o0.xyz = r3.xyz * r0.yyy + -r1.xyz;
  r0.xyz = cb0[3].xyz * r0.xzw;
  r0.xz = r0.xx + r0.yz;
  r0.y = r0.y * r0.z;
  r0.x = r0.w * cb0[3].z + r0.x;
  r0.y = sqrt(r0.y);
  r0.y = dot(cb0[3].ww, r0.yy);
  r0.x = r0.x + r0.y;
  o0.w = cb0[27].z * r0.x;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}