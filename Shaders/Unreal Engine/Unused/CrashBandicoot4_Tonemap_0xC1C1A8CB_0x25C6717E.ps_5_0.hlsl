Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // Bloomed Scene
Texture2D<float4> t2 : register(t2); // Exposure?
Texture3D<float4> t3 : register(t3); // LUT

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);
SamplerState s3_s : register(s3);

cbuffer cb1 : register(b1)
{
  float4 cb1[135];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[66];
}

void main(
  linear noperspective float2 v0 : TEXCOORD0,
  linear noperspective float2 w0 : TEXCOORD3,
  linear noperspective float4 v1 : TEXCOORD1,
  linear noperspective float4 v2 : TEXCOORD2,
  float2 v3 : TEXCOORD4,
  float4 v4 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2 = 0; // Init to avoid warnings below
  r0.x = v2.w * 543.309998 + v2.z;
  r0.x = sin(r0.x);
  r0.x = 493013 * r0.x;
  r0.x = frac(r0.x);
#if _25C6717E
  r0.yzw = t0.Sample(s0_s, v0.xy).xyz;
#elif _C1C1A8CB
  //r1.xyzw = cmp(float4(0,0,0,0) < w0.xyzw); // ?
  //r2.xyzw = cmp(w0.xyzw < float4(0,0,0,0)); // ?
  r1.xyzw = (int4)-r1.xyzw + (int4)r2.xyzw;
  r1.xyzw = (int4)r1.xyzw;
  //r2.xyzw = saturate(-cb0[67].zzzz + abs(w0.xyzw)); // ?
  r1.xyzw = r2.xyzw * r1.xyzw;
  //r1.xyzw = -r1.xyzw * cb0[67].xxyy + w0.xyzw; // ?
  r1.xyzw = r1.xyzw * cb0[38].zwzw + cb0[39].xyxy;
  r1.xyzw = cb0[38].xyxy * r1.xyzw;
  r0.y = t0.Sample(s0_s, r1.xy).x;
  r0.z = t0.Sample(s0_s, r1.zw).y;
  r0.w = t0.Sample(s0_s, v0.xy).z;
#endif
  r0.yzw = cb1[134].zzz * r0.yzw;
  r1.xy = max(cb0[50].zw, v0.xy);
  r1.xy = min(cb0[51].xy, r1.xy);
  r1.xyz = t1.Sample(s1_s, r1.xy).xyz;
  r1.xyz = cb1[134].zzz * r1.xyz;
  r2.xy = asuint(cb0[53].zw);
  r2.xy = v4.xy + -r2.xy;
  r2.xy = cb0[55].xy * r2.xy;
  r2.xyz = t2.Sample(s2_s, r2.xy).xyz;
  r2.xyz = r2.xyz * cb0[65].xyz + cb0[60].xyz;
  r1.xyz = r2.xyz * r1.xyz;
  r0.yzw = r0.yzw * cb0[59].xyz + r1.xyz;
  r0.yzw = v1.xxx * r0.yzw;
  r1.xy = cb0[61].xx * v1.yz;
  r1.x = dot(r1.xy, r1.xy);
  r1.x = 1 + r1.x;
  r1.x = rcp(r1.x);
  r1.x = r1.x * r1.x;
  
  //o0.xyz = r0.yzw; return;

  r0.yzw = r0.yzw * r1.xxx + float3(0.00266771927,0.00266771927,0.00266771927);
  r0.yzw = log2(r0.yzw);
  r0.yzw = saturate(r0.yzw * float3(0.0714285746,0.0714285746,0.0714285746) + float3(0.610726953,0.610726953,0.610726953));
  r0.yzw = r0.yzw * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r0.yzw = t3.Sample(s3_s, r0.yzw).xyz;
  r1.xyz = float3(1.04999995,1.04999995,1.04999995) * r0.yzw;
  //o0.w = saturate(GetLuminance(r1.xyz)); // Fixed bad BT.601 luminance formula
  o0.w = saturate(dot(r1.xyz, float3(0.298999995,0.587000012,0.114)));
  r0.x = r0.x * 0.00390625 + -0.001953125;
  r0.xyz = r0.yzw * float3(1.04999995,1.04999995,1.04999995) + r0.xxx;
  // Unused branch
  if (cb0[64].x != 0) {
    // LinearToST2084
    r1.xyz = log2(r0.xyz);
    r1.xyz = float3(0.0126833133,0.0126833133,0.0126833133) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r2.xyz = float3(-0.8359375,-0.8359375,-0.8359375) + r1.xyz;
    r2.xyz = max(float3(0,0,0), r2.xyz);
    r1.xyz = -r1.xyz * float3(18.6875,18.6875,18.6875) + float3(18.8515625,18.8515625,18.8515625);
    r1.xyz = r2.xyz / r1.xyz;
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(6.27739477,6.27739477,6.27739477) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = float3(10000,10000,10000) * r1.xyz;
    r1.xyz = r1.xyz / cb0[63].www;
    r1.xyz = max(float3(6.10351999e-005,6.10351999e-005,6.10351999e-005), r1.xyz);
    r2.xyz = float3(12.9200001,12.9200001,12.9200001) * r1.xyz;
    r1.xyz = max(float3(0.00313066994,0.00313066994,0.00313066994), r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
    o0.xyz = min(r2.xyz, r1.xyz);
  } else {
    o0.xyz = r0.xyz;
    //o0.xyz = 0; return;
  }
}