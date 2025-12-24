Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[68];
}

void main(
  float4 v0 : COLOR0,
  float3 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD7,
  float4 v5 : TEXCOORD8,
  float4 v6 : SV_Position0,
  uint v7 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  r0.x = dot(v2.xyz, v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v2.xyz * r0.xxx;
  r0.w = dot(r0.xyz, v1.xyz);
  r1.xyz = -r0.www * r0.xyz + v1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = r1.yzx * r0.zxy;
  r2.xyz = r0.yzx * r1.zxy + -r2.xyz;
  r2.xyz = v2.www * r2.xyz;
  r3.xy = v4.zw / v5.ww;
  r4.xyz = cb0[5].xyz + v5.xyz;
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r5.xyz = v5.xyz * r0.www;
  r3.zw = v3.xy * cb0[26].xy + cb0[26].zw;
  r6.xyz = t2.Sample(s3_s, r3.zw).xyz;
  r6.xyz = r6.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r6.xy = cb0[29].xx * r6.xy;
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r6.xyz = r6.xyz * r0.www;
  r2.xyz = r6.yyy * r2.xyz;
  r1.xyz = r6.xxx * r1.xyz + r2.xyz;
  r0.xyz = r6.zzz * r0.xyz + r1.xyz;
  if (cb0[12].x != 0) {
    r1.xy = v6.xy * cb0[14].zw + cb0[14].xy;
    r0.w = t3.SampleLevel(s2_s, r1.xy, 0).x;
    r0.w = cb0[13].y * r0.w + cb0[13].z;
    r1.x = 1 + -cb0[13].x;
    r0.w = -r1.x + r0.w;
  } else {
    r0.w = 1;
  }
  r0.w = (r0.w < 0);
  if (r0.w != 0) discard;
  r0.w = saturate(-v5.w * cb0[11].w + 1);
  r1.x = r0.w * r0.w;
  r1.y = dot(-r5.xyz, r0.xyz);
  r1.z = r1.y * r1.y;
  r1.y = r1.z * r1.y + 0.0199999996;
  r0.w = dot(r0.ww, r1.xx);
  r0.w = min(1, r0.w);
  r0.w = r1.y * r0.w;
  r1.xyz = r0.www * cb0[11].xyz + cb0[65].xyz;
  r2.x = saturate(dot(r6.yz, float2(0.816496611,0.577350259)));
  r2.y = saturate(dot(r6.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r2.z = saturate(dot(r6.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r2.xyz = r2.xyz * r2.xyz;
  r2.xyz = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r2.xyz);
  r5.xyz = t0.Sample(s0_s, v4.xy).xyz;
  r6.xyz = t1.Sample(s1_s, v4.xy).xyz;
  r5.xyz = cb0[66].xyz * r5.xyz;
  r6.xyz = cb0[67].xyz * r6.xyz;
  r0.w = dot(r6.xyz, r2.xyz);
  r1.xyz = r5.xyz * r0.www + r1.xyz;
  r2.xy = r3.xy * cb2[0].xy + cb2[0].wz;
  r2.xyzw = t4.Sample(s5_s, r2.xy).xyzw;
  r2.xyzw = cb0[63].xyzw * r2.xyzw + cb0[62].xyzw;
  r3.xyz = -r4.xyz * cb0[51].www + cb0[51].xyz;
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.w);
  r5.xyz = r3.xyz * r0.www;
  r3.x = dot(r5.xyz, r3.xyz);
  r5.x = saturate(dot(r0.xyz, r5.xyz));
  r6.xyz = -r4.xyz * cb0[52].www + cb0[52].xyz;
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r7.xyz = r6.xyz * r0.www;
  r3.y = dot(r7.xyz, r6.xyz);
  r5.y = saturate(dot(r0.xyz, r7.xyz));
  r6.xyz = -r4.xyz * cb0[53].www + cb0[53].xyz;
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r7.xyz = r6.xyz * r0.www;
  r3.z = dot(r7.xyz, r6.xyz);
  r5.z = saturate(dot(r0.xyz, r7.xyz));
  r4.xyz = -r4.xyz * cb0[54].www + cb0[54].xyz;
  r0.w = dot(r4.xyz, r4.xyz);
  r0.w = rsqrt(r0.w);
  r6.xyz = r4.xyz * r0.www;
  r3.w = dot(r6.xyz, r4.xyz);
  r5.w = saturate(dot(r0.xyz, r6.xyz));
  r0.xyzw = saturate(r3.xyzw * cb0[60].xyzw + cb0[61].xyzw);
  r0.xyzw = -r0.xyzw * r0.xyzw + float4(1,1,1,1);
  r0.xyzw = log2(r0.xyzw);
  r0.xyzw = cb0[59].xyzw * r0.xyzw;
  r0.xyzw = exp2(r0.xyzw);
  r0.xyzw = r0.xyzw * r2.xyzw;
  r0.xyzw = r5.xyzw * r0.xyzw;
  r2.xyzw = cb0[56].xyzw * r0.yyyy;
  r2.xyzw = cb0[55].xyzw * r0.xxxx + r2.xyzw;
  r2.xyzw = cb0[57].xyzw * r0.zzzz + r2.xyzw;
  r0.xyzw = cb0[58].xyzw * r0.wwww + r2.xyzw;
  r1.w = 0;
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.w = saturate(1 + -r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.xyz = float3(1,1,1) + -cb0[25].xyz;
  r2.xy = v3.xy * cb0[27].xy + cb0[27].zw;
  r2.xyz = t5.Sample(s4_s, r2.xy).xyz;
  r3.xyz = cb0[28].www * cb0[28].xyz;
  r2.xyz = r3.xyz * r2.xyz;
  r1.xyz = r2.xyz * r1.xyz;
  r0.xyz = (r1.xyz * r0.xyz); // Luma: removed saturate
  o0.xyz = cb0[25].xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}