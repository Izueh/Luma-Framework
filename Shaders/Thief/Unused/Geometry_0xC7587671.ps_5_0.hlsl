Texture2D<float4> t10 : register(t10);
Texture2D<float4> t9 : register(t9);
Texture2D<float4> t8 : register(t8);
Texture2D<float4> t7 : register(t7);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s10_s : register(s10);
SamplerState s9_s : register(s9);
SamplerState s8_s : register(s8);
SamplerState s7_s : register(s7);
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
  float4 cb0[73];
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
  r3.zw = v3.xy * cb0[27].xy + cb0[27].zw;
  r3.zw = t3.Sample(s4_s, r3.zw).xy;
  r3.zw = r3.zw * float2(2,2) + float2(-1,-1);
  r3.zw = cb0[37].yy * r3.zw;
  r7.xy = v3.xy * cb0[28].xy + cb0[28].zw;
  r0.w = t4.Sample(s5_s, r7.xy).y;
  r1.w = v0.x * 2 + -1;
  r1.w = 1 + -abs(r1.w);
  r1.w = saturate(r1.w + r1.w);
  r0.w = r0.w * 2 + cb0[37].w;
  r0.w = -1 + r0.w;
  r0.w = cb0[37].z * r0.w;
  r0.w = saturate(r0.w * r1.w + v0.x);
  r3.zw = r3.zw * r0.ww;
  r6.xy = r6.xy * cb0[37].xx + r3.zw;
  r1.w = dot(r6.xyz, r6.xyz);
  r1.w = rsqrt(r1.w);
  r6.xyz = r6.xyz * r1.www;
  r7.xyz = r6.yyy * r2.xyz;
  r7.xyz = r6.xxx * r1.xyz + r7.xyz;
  r7.xyz = r6.zzz * r0.xyz + r7.xyz;
  r1.w = dot(r7.xyz, -r5.xyz);
  r8.xyz = r7.xyz * r1.www;
  r5.xyz = r8.xyz * float3(2,2,2) + r5.xyz;
  r1.x = dot(r1.xyz, r5.xyz);
  r1.y = dot(r2.xyz, r5.xyz);
  r1.z = dot(r0.xyz, r5.xyz);
  if (cb0[12].x != 0) {
    r0.xy = v6.xy * cb0[14].zw + cb0[14].xy;
    r0.x = t5.SampleLevel(s2_s, r0.xy, 0).x;
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
  r0.z = r1.w * r1.w;
  r0.z = r0.z * r1.w + 0.0199999996;
  r0.x = dot(r0.xx, r0.yy);
  r0.x = min(1, r0.x);
  r0.x = r0.z * r0.x;
  r0.xyz = r0.xxx * cb0[11].xyz + cb0[70].xyz;
  r1.w = cb0[38].x + -cb0[38].y;
  r1.w = r0.w * r1.w + cb0[38].y;
  r2.x = saturate(dot(r6.yz, float2(0.816496611,0.577350259)));
  r2.y = saturate(dot(r6.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r2.z = saturate(dot(r6.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r6.x = saturate(dot(r1.yz, float2(0.816496611,0.577350259)));
  r6.y = saturate(dot(r1.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r6.z = saturate(dot(r1.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r1.xyz = r2.xyz * r2.xyz;
  r1.xyz = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r1.xyz);
  r2.x = 1 + r1.w;
  r2.yzw = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r6.xyz);
  r2.yzw = log2(r2.yzw);
  r2.xyz = r2.xxx * r2.yzw;
  r2.xyz = exp2(r2.xyz);
  r6.xyz = t0.Sample(s0_s, v4.xy).xyz;
  r8.xyz = t1.Sample(s1_s, v4.xy).xyz;
  r6.xyz = cb0[71].xyz * r6.xyz;
  r8.xyz = cb0[72].xyz * r8.xyz;
  r1.x = dot(r8.xyz, r1.xyz);
  r9.xyz = r6.xyz * r1.xxx + r0.xyz;
  r0.x = dot(r8.xyz, r2.xyz);
  r0.yz = r3.xy * cb2[0].xy + cb2[0].wz;
  r0.yz = t6.Sample(s10_s, r0.yz).xy;
  r0.yz = cb0[68].xy * r0.yz + cb0[67].xy;
  r1.xyz = -r4.xyz * cb0[60].www + cb0[60].xyz;
  r2.x = dot(r1.xyz, r1.xyz);
  r2.x = rsqrt(r2.x);
  r2.xyz = r2.xxx * r1.xyz;
  r1.x = dot(r2.xyz, r1.xyz);
  r3.x = saturate(dot(r7.xyz, r2.xyz));
  r2.x = saturate(dot(r5.xyz, r2.xyz));
  r4.xyz = -r4.xyz * cb0[61].www + cb0[61].xyz;
  r1.z = dot(r4.xyz, r4.xyz);
  r1.z = rsqrt(r1.z);
  r8.xyz = r4.xyz * r1.zzz;
  r1.y = dot(r8.xyz, r4.xyz);
  r3.y = saturate(dot(r7.xyz, r8.xyz));
  r2.y = saturate(dot(r5.xyz, r8.xyz));
  r1.xy = saturate(r1.xy * cb0[65].xy + cb0[66].xy);
  r1.xy = -r1.xy * r1.xy + float2(1,1);
  r1.xy = log2(r1.xy);
  r1.xy = cb0[64].xy * r1.xy;
  r1.xy = exp2(r1.xy);
  r0.yz = r1.xy * r0.yz;
  r1.xy = r3.xy * r0.yz;
  r2.xy = log2(r2.xy);
  r1.zw = r2.xy * r1.ww;
  r1.zw = exp2(r1.zw);
  r0.yz = r1.zw * r0.yz;
  r2.xyzw = cb0[63].xyzw * r1.yyyy;
  r1.xyzw = cb0[62].xyzw * r1.xxxx + r2.xyzw;
  r2.xyz = cb0[63].xyz * r0.zzz;
  r2.xyz = cb0[62].xyz * r0.yyy + r2.xyz;
  r9.w = 0;
  r1.xyzw = r9.xyzw + r1.xyzw;
  r0.xyz = r6.xyz * r0.xxx + r2.xyz;
  r1.w = saturate(1 + -r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.xyz = float3(1,1,1) + -cb0[25].xyz;
  r3.xy = v3.xy * cb0[29].xy + cb0[29].zw;
  r3.xyz = t7.Sample(s6_s, r3.xy).xyz;
  r4.xyz = cb0[30].www * cb0[30].xyz;
  r3.xyz = r4.xyz * r3.xyz;
  r4.xy = v3.xy * cb0[31].xy + cb0[31].zw;
  r4.xyz = t8.Sample(s7_s, r4.xy).xyz;
  r5.xyz = cb0[32].www * cb0[32].xyz;
  r4.xyz = r4.xyz * r5.xyz + -r3.xyz;
  r3.xyz = r0.www * r4.xyz + r3.xyz;
  r2.xyz = r3.xyz * r2.xyz;
  r1.xyz = (r2.xyz * r1.xyz); // Luma: removed saturate
  r2.xy = v3.xy * cb0[33].xy + cb0[33].zw;
  r2.xyz = t9.Sample(s8_s, r2.xy).xyz;
  r3.xyz = cb0[34].www * cb0[34].xyz;
  r2.xyz = r3.xyz * r2.xyz;
  r3.xy = v3.xy * cb0[35].xy + cb0[35].zw;
  r3.xyz = t10.Sample(s9_s, r3.xy).xyz;
  r4.xyz = cb0[36].www * cb0[36].xyz;
  r3.xyz = r3.xyz * r4.xyz + -r2.xyz;
  r2.xyz = r0.www * r3.xyz + r2.xyz;
  r0.xyz = (r2.xyz * r0.xyz); // Luma: removed saturate
  r1.xyz = cb0[25].xyz + r1.xyz;
  o0.xyz = r1.xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}