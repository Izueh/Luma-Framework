Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s6_s : register(s6);
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
  float4 cb0[66];
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
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
  r6.xy = cb0[31].xx * r6.xy;
  r0.w = dot(r6.xyz, r6.xyz);
  r0.w = rsqrt(r0.w);
  r6.xyz = r6.xyz * r0.www;
  r7.xyz = r6.yyy * r2.xyz;
  r7.xyz = r6.xxx * r1.xyz + r7.xyz;
  r7.xyz = r6.zzz * r0.xyz + r7.xyz;
  r0.w = dot(r7.xyz, -r5.xyz);
  r8.xyz = r7.xyz * r0.www;
  r5.xyz = r8.xyz * float3(2,2,2) + r5.xyz;
  r1.x = dot(r1.xyz, r5.xyz);
  r1.y = dot(r2.xyz, r5.xyz);
  r1.z = dot(r0.xyz, r5.xyz);
  if (cb0[12].x != 0) {
    r0.xy = v6.xy * cb0[14].zw + cb0[14].xy;
    r0.x = t3.SampleLevel(s2_s, r0.xy, 0).x;
    r0.x = cb0[13].y * r0.x + cb0[13].z;
    r0.y = 1 + -cb0[13].x;
    r0.x = r0.x + -r0.y;
  } else {
    r0.x = 1;
  }
  r0.x = (r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = saturate(-v5.w * cb0[11].w + 1);
  r0.y = r0.x * r0.x;
  r0.z = r0.w * r0.w;
  r0.z = r0.z * r0.w + 0.0199999996;
  r0.x = dot(r0.xx, r0.yy);
  r0.x = min(1, r0.x);
  r0.x = r0.z * r0.x;
  r0.xyz = r0.xxx * cb0[11].xyz + cb0[63].xyz;
  r2.x = saturate(dot(r6.yz, float2(0.816496611,0.577350259)));
  r2.y = saturate(dot(r6.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r2.z = saturate(dot(r6.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r6.x = saturate(dot(r1.yz, float2(0.816496611,0.577350259)));
  r6.y = saturate(dot(r1.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r6.z = saturate(dot(r1.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r1.xyz = r2.xyz * r2.xyz;
  r1.xyz = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r1.xyz);
  r0.w = 1 + cb0[31].y;
  r2.xyz = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r6.xyz);
  r2.xyz = log2(r2.xyz);
  r2.xyz = r2.xyz * r0.www;
  r2.xyz = exp2(r2.xyz);
  r6.xyz = t0.Sample(s0_s, v4.xy).xyz;
  r8.xyz = t1.Sample(s1_s, v4.xy).xyz;
  r6.xyz = cb0[64].xyz * r6.xyz;
  r8.xyz = cb0[65].xyz * r8.xyz;
  r0.w = dot(r8.xyz, r1.xyz);
  r0.xyz = r6.xyz * r0.www + r0.xyz;
  r1.x = dot(r8.xyz, r2.xyz);
  r1.yz = r3.xy * cb2[0].xy + cb2[0].wz;
  r1.yz = t4.Sample(s6_s, r1.yz).xy;
  r1.yz = cb0[61].xy * r1.yz + cb0[60].xy;
  r2.xyz = -r4.xyz * cb0[53].www + cb0[53].xyz;
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = r2.xyz * r1.www;
  r2.x = dot(r3.xyz, r2.xyz);
  r8.x = saturate(dot(r7.xyz, r3.xyz));
  r3.x = saturate(dot(r5.xyz, r3.xyz));
  r4.xyz = -r4.xyz * cb0[54].www + cb0[54].xyz;
  r1.w = dot(r4.xyz, r4.xyz);
  r1.w = rsqrt(r1.w);
  r9.xyz = r4.xyz * r1.www;
  r2.y = dot(r9.xyz, r4.xyz);
  r8.y = saturate(dot(r7.xyz, r9.xyz));
  r3.y = saturate(dot(r5.xyz, r9.xyz));
  r2.xy = saturate(r2.xy * cb0[58].xy + cb0[59].xy);
  r2.xy = -r2.xy * r2.xy + float2(1,1);
  r2.xy = log2(r2.xy);
  r2.xy = cb0[57].xy * r2.xy;
  r2.xy = exp2(r2.xy);
  r1.yz = r2.xy * r1.yz;
  r2.xy = r8.xy * r1.yz;
  r2.zw = log2(r3.xy);
  r2.zw = cb0[31].yy * r2.zw;
  r2.zw = exp2(r2.zw);
  r1.yz = r2.zw * r1.yz;
  r3.xyzw = cb0[56].xyzw * r2.yyyy;
  r2.xyzw = cb0[55].xyzw * r2.xxxx + r3.xyzw;
  r3.xyz = cb0[56].xyz * r1.zzz;
  r1.yzw = cb0[55].xyz * r1.yyy + r3.xyz;
  r0.w = 0;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r1.xyz = r6.xyz * r1.xxx + r1.yzw;
  r0.w = saturate(1 + -r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.xyz = float3(1,1,1) + -cb0[25].xyz;
  r3.xy = v3.xy * cb0[27].xy + cb0[27].zw;
  r3.xyz = t5.Sample(s4_s, r3.xy).xyz;
  r4.xyz = cb0[28].www * cb0[28].xyz;
  r3.xyz = r4.xyz * r3.xyz;
  r2.xyz = r3.xyz * r2.xyz;
  r0.xyz = (r2.xyz * r0.xyz); // Luma: removed saturate
  r2.xy = v3.xy * cb0[29].xy + cb0[29].zw;
  r2.xyz = t6.Sample(s5_s, r2.xy).xyz;
  r3.xyz = cb0[30].www * cb0[30].xyz;
  r2.xyz = r3.xyz * r2.xyz;
  r1.xyz = (r2.xyz * r1.xyz); // Luma: removed saturate
  r0.xyz = cb0[25].xyz + r0.xyz;
  o0.xyz = r0.xyz + r1.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}