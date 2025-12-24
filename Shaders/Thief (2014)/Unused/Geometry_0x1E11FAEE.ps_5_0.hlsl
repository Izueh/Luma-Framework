TextureCube<float4> t6 : register(t6);
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

cbuffer cb0 : register(b0)
{
  float4 cb0[58];
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
  float4 r0,r1,r2,r3,r4,r5,r6;
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
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = v5.xyz * r0.www;
  r4.xy = cb0[26].xy * v3.xy;
  r4.xy = t2.Sample(s4_s, r4.xy).xy;
  r4.xy = r4.xy * float2(2,2) + float2(-1,-1);
  r4.zw = v3.xy * cb0[27].xy + cb0[27].zw;
  r5.xyz = t3.Sample(s5_s, r4.zw).xyz;
  r5.xyz = r5.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r4.zw = v3.xy * cb0[28].xy + cb0[28].zw;
  r6.xyzw = t4.Sample(s6_s, r4.zw).xyzw;
  r0.w = 1 + -r6.w;
  r4.xy = cb0[32].yy * r4.xy;
  r1.w = 0.100000001 * r0.w;
  r4.xy = r4.xy * r1.ww;
  r5.xy = r5.xy * cb0[32].xx + r4.xy;
  r1.w = dot(r5.xyz, r5.xyz);
  r1.w = rsqrt(r1.w);
  r4.xyz = r5.xyz * r1.www;
  r2.xyz = r4.yyy * r2.xyz;
  r1.xyz = r4.xxx * r1.xyz + r2.xyz;
  r0.xyz = r4.zzz * r0.xyz + r1.xyz;
  r1.x = dot(r0.xyz, -r3.xyz);
  r0.xyz = r1.xxx * r0.xyz;
  r0.xyz = r0.xyz * float3(2,2,2) + r3.xyz;
  if (cb0[12].x != 0) {
    r1.yz = v6.xy * cb0[14].zw + cb0[14].xy;
    r1.y = t5.SampleLevel(s2_s, r1.yz, 0).x;
    r1.y = cb0[13].y * r1.y + cb0[13].z;
    r1.z = 1 + -cb0[13].x;
    r1.y = r1.y + -r1.z;
  } else {
    r1.y = 1;
  }
  r1.y = (r1.y < 0);
  if (r1.y != 0) discard;
  r1.yzw = cb0[29].www * cb0[29].xyz;
  r1.yzw = r0.www * r1.yzw + cb0[25].xyz;
  r2.x = saturate(-v5.w * cb0[11].w + 1);
  r2.y = r2.x * r2.x;
  r2.z = r1.x * r1.x;
  r1.x = r2.z * r1.x + 0.0199999996;
  r2.x = dot(r2.xx, r2.yy);
  r2.x = min(1, r2.x);
  r1.x = r2.x * r1.x;
  r2.xyz = r1.xxx * cb0[11].xyz + cb0[55].xyz;
  r3.x = saturate(dot(r4.yz, float2(0.816496611,0.577350259)));
  r3.y = saturate(dot(r4.xyz, float3(-0.707106769,-0.408248305,0.577350259)));
  r3.z = saturate(dot(r4.yzx, float3(-0.408248305,0.577350259,0.707106769)));
  r3.xyz = r3.xyz * r3.xyz;
  r3.xyz = max(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007), r3.xyz);
  r4.xyz = t0.Sample(s0_s, v4.xy).xyz;
  r5.xyz = t1.Sample(s1_s, v4.xy).xyz;
  r4.xyz = cb0[56].xyz * r4.xyz;
  r5.xyz = cb0[57].xyz * r5.xyz;
  r1.x = dot(r5.xyz, r3.xyz);
  r2.xyz = r4.xyz * r1.xxx + r2.xyz;
  r3.xyz = float3(1,1,1) + -cb0[25].xyz;
  r4.xyz = cb0[30].www * cb0[30].xyz;
  r4.xyz = r6.xyz * r4.xyz;
  r0.xyz = t6.Sample(s3_s, r0.xyz).xyz;
  r5.xyz = cb0[31].www * cb0[31].xyz;
  r0.xyz = r5.xyz * r0.xyz;
  r0.xyz = r0.www * r0.xyz;
  r0.xyz = r4.xyz * r3.xyz + r0.xyz;
  r0.xyz = (r2.xyz * r0.xyz); // Luma: removed saturate
  o0.xyz = r1.yzw + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}