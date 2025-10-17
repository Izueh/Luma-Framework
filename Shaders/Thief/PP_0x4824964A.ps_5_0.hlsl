Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

void main(
  float4 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  r0.xy = t1.Sample(s1_s, v0.zw).xy;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.z = dot(r0.xy, r0.xy);
  r1.xyzw = cb0[1].xyxy * r0.xyxy;
  r0.x = min(1, r0.z);
  r0.xyzw = r1.xyzw * r0.xxxx;
  r1.xy = float2(0.125,0.125) * r0.zw;
  r2.xyzw = cb0[2].zwzw + -cb0[2].xyxy;
  r1.xy = r1.xy * r2.xy + v0.zw;
  r1.xy = max(cb0[2].xy, r1.xy);
  r1.xy = min(cb0[2].zw, r1.xy);
  r1.xyz = t0.Sample(s0_s, r1.xy).xyz;
  r1.xyz = float3(0.150000006,0.150000006,0.150000006) * r1.xyz;
  r3.xyz = t0.Sample(s0_s, v0.zw).xyz;
  r1.xyz = r3.xyz * float3(0.400000006,0.400000006,0.400000006) + r1.xyz;
  r3.xy = r2.zw * r0.zw;
  r0.xyzw = r2.xyzw * -r0.xyzw;
  r2.xyzw = r3.xyxy * float4(0.25,0.25,0.330000013,0.330000013) + v0.zwzw;
  r3.xy = r3.xy * float2(0.5,0.5) + v0.zw;
  r3.xy = max(cb0[2].xy, r3.xy);
  r3.xy = min(cb0[2].zw, r3.xy);
  r3.xyz = t0.Sample(s0_s, r3.xy).xyz;
  r2.xyzw = max(cb0[2].xyxy, r2.xyzw);
  r2.xyzw = min(cb0[2].zwzw, r2.xyzw);
  r4.xyz = t0.Sample(s0_s, r2.xy).xyz;
  r2.xyz = t0.Sample(s0_s, r2.zw).xyz;
  r1.xyz = r4.xyz * float3(0.075000003,0.075000003,0.075000003) + r1.xyz;
  r1.xyz = r2.xyz * float3(0.0500000007,0.0500000007,0.0500000007) + r1.xyz;
  r1.xyz = r3.xyz * float3(0.0250000004,0.0250000004,0.0250000004) + r1.xyz;
  r2.xyzw = r0.zwzw * float4(0.125,0.125,0.25,0.25) + v0.zwzw;
  r0.xyzw = r0.xyzw * float4(0.330000013,0.330000013,0.5,0.5) + v0.zwzw;
  r0.xyzw = max(cb0[2].xyxy, r0.xyzw);
  r0.xyzw = min(cb0[2].zwzw, r0.xyzw);
  r2.xyzw = max(cb0[2].xyxy, r2.xyzw);
  r2.xyzw = min(cb0[2].zwzw, r2.xyzw);
  r3.xyz = t0.Sample(s0_s, r2.xy).xyz;
  r2.xyz = t0.Sample(s0_s, r2.zw).xyz;
  r1.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + r1.xyz;
  r1.xyz = r2.xyz * float3(0.075000003,0.075000003,0.075000003) + r1.xyz;
  r2.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = t0.Sample(s0_s, r0.zw).xyz;
  r1.xyz = r2.xyz * float3(0.0500000007,0.0500000007,0.0500000007) + r1.xyz;
  o0.xyz = r0.xyz * float3(0.0250000004,0.0250000004,0.0250000004) + r1.xyz;
  o0.w = 1;
}