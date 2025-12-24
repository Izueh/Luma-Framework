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

cbuffer cb0 : register(b0)
{
  float4 cb0[30];
}

// TODO: fix this for UW, it trails behind and probably stretches too
void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD2,
  float4 v2 : TEXCOORD8,
  float2 v3 : TEXCOORD9,
  uint v4 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2,r3,r4;
  r0.x = 0.5;
  r1.xyzw = cb0[24].xyxy + v1.xyxy;
  r2.xzw = float3(1,1.39999998,0.699999988) * r1.xzw;
  r3.w = 0;
  r4.xyz = cb0[28].xxx * cb0[25].zzy;
  r3.xyz = float3(0.100000001,-0.100000001,0.100000001) * r4.xyz;
  r0.zw = r3.wy;
  r4.zw = float2(1,-1) * r3.wz;
  r2.y = r1.y * 0.5 + r3.x;
  r0.xyzw = r2.xyzw + r0.xxzw;
  r2.xy = t2.Sample(s2_s, r0.xy).xy;
  r2.zw = t2.Sample(s2_s, r0.zw).xy;
  r0.xyzw = r2.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r0.xy = r0.xy + r0.zw;
  r2.xyzw = cb0[25].xxxx * r0.xyxy;
  r1.xyzw = r1.zwzw * float4(2,1,2,1) + r2.xyzw;
  r2.y = r1.y * 1.33299994 + r3.z;
  r2.xzw = float3(1.33299994,0.77700001,0.77700001) * r1.xzw;
  r4.x = 0.5;
  r1.xyzw = r2.xyzw + r4.xxzw;
  r0.z = t3.Sample(s3_s, r1.xy).x;
  r0.w = t3.Sample(s3_s, r1.zw).x;
  r0.z = r0.z + r0.w;
  r0.w = abs(cb0[26].z) + abs(cb0[26].w);
  r1.xy = r0.ww * float2(2,3) + cb0[25].ww;
  r1.zw = v1.xy * float2(2,2) + float2(-1,-1);
  r0.xy = r0.xy * r1.xy + r1.zw;
  r0.xy = r0.xy * cb0[26].xy + cb0[26].zw;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = min(1, r0.x);
  r0.x = r0.x * r0.x;
  r0.y = r0.z * r0.x;
  r0.x = r0.x * r0.x;
  r0.x = cb0[27].y * r0.x;
  r0.x = saturate(cb0[27].x * r0.y + r0.x);
  r0.x = cb0[28].w * r0.x;
  r0.x = cb0[23].w * r0.x;
  r0.yz = v3.xy / v2.ww;
  r0.yz = r0.yz * cb2[0].xy + cb2[0].wz;
  r1.xyz = t1.Sample(s0_s, r0.yz).xyz;
  r2.xyz = cb0[23].xyz + -r1.xyz;
  r1.xyz = r0.xxx * r2.xyz + r1.xyz;
  r0.x = 10 * cb0[28].x;
  r0.x = trunc(r0.x);
  r0.x = 0.333299994 * r0.x;
  r0.xy = r0.yz * float2(24,13.4998312) + r0.xx;
  r0.x = t0.Sample(s1_s, r0.xy).x;
  r0.x = r0.x * 2 + -1;
  r0.y = 0.000319999992 * cb0[29].x;
  r0.xyz = r0.yyy * r0.xxx + r1.xyz;
  o0.xyz = cb0[22].xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}