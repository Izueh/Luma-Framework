Texture3D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[136];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[71];
}

#define cmp -

void main(
  linear noperspective float2 v0 : TEXCOORD0,
  linear noperspective float2 w0 : TEXCOORD3,
  linear noperspective float4 v1 : TEXCOORD1,
  linear noperspective float4 v2 : TEXCOORD2,
  float2 v3 : TEXCOORD4,
  float4 v4 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  r0.x = v2.w * 543.309998 + v2.z;
  r0.x = sin(r0.x);
  r0.x = 493013 * r0.x;
  r0.x = frac(r0.x);
  r0.y = -r0.x * r0.x + 1;
  r0.y = cb0[64].z * r0.y;
  r1.xyzw = v2.xyxy + -v0.xyxy;
  r2.xyzw = r1.xyzw * r0.yyyy;
  r0.yz = r0.yy * r1.zw + v0.xy;
  r1.xy = w0.xy * cb0[67].zw + cb0[67].xy;
  r3.xyzw = cmp(float4(0,0,0,0) < r1.xyxy);
  r4.xyzw = cmp(r1.xyxy < float4(0,0,0,0));
  r3.xyzw = (int4)-r3.xyzw + (int4)r4.xyzw;
  r3.xyzw = (int4)r3.xyzw;
  r4.xyzw = saturate(-cb0[70].zzzz + abs(r1.xyxy));
  r3.xyzw = r4.xyzw * r3.xyzw;
  r3.xyzw = -r3.xyzw * cb0[70].xxyy + r1.xyxy;
  r3.xyzw = r3.xyzw * cb0[68].zwzw + cb0[68].xyxy;
  r3.xyzw = r3.xyzw * cb0[38].zwzw + cb0[39].xyxy;
  r2.xyzw = r3.xyzw * cb0[38].xyxy + r2.xyzw;
  r2.xyzw = max(cb0[43].zwzw, r2.xyzw);
  r2.xyzw = min(cb0[44].xyxy, r2.xyzw);
  r3.x = t0.Sample(s0_s, r2.xy).x;
  r3.y = t0.Sample(s0_s, r2.zw).y;
  r1.zw = max(cb0[43].zw, r0.yz);
  r1.zw = min(cb0[44].xy, r1.zw);
  r3.z = t0.Sample(s0_s, r1.zw).z;
  r2.xyz = cb1[135].zzz * r3.xyz;
  r0.w = dot(r2.xyz, float3(0.300000012,0.589999974,0.109999999));
  r1.zw = cb0[37].yz * r0.yz;
  r1.zw = floor(r1.zw);
  r1.zw = (uint2)r1.zw;
  r1.zw = (int2)r1.zw & int2(1,1);
  r1.zw = (uint2)r1.zw;
  r3.xy = r1.zw * float2(2,2) + float2(-1,-1);
  r4.x = cb0[38].x * r3.x;
  r4.y = 0;
  r1.zw = r4.xy + r0.yz;
  r4.xyz = t0.Sample(s0_s, r1.zw).xyz;
  r5.xyz = cb1[135].zzz * r4.xyz;
  r3.z = 0;
  r0.yz = r3.zy * cb0[38].xy + r0.yz;
  r6.xyz = t0.Sample(s0_s, r0.yz).xyz;
  r7.xyz = cb1[135].zzz * r6.xyz;
  r5.x = dot(r5.xyz, float3(0.300000012,0.589999974,0.109999999));
  r5.y = dot(r7.xyz, float3(0.300000012,0.589999974,0.109999999));
  r7.xyz = ddx_fine(r2.xyz);
  r7.xyz = -r7.xyz * r3.xxx + r2.xyz;
  r8.xyz = ddy_fine(r2.xyz);
  r8.xyz = -r8.xyz * r3.yyy + r2.xyz;
  r0.y = ddx_fine(r0.w);
  r5.z = -r0.y * r3.x + r0.w;
  r0.y = ddy_fine(r0.w);
  r5.w = -r0.y * r3.y + r0.w;
  r3.xyzw = -r5.xyzw + r0.wwww;
  r0.yz = max(abs(r3.xz), abs(r3.yw));
  r0.y = max(r0.y, r0.z);
  r0.y = saturate(-v1.x * r0.y + 1);
  r0.y = cb0[62].y * -r0.y;
  r3.xyz = r4.xyz * cb1[135].zzz + r7.xyz;
  r3.xyz = r6.xyz * cb1[135].zzz + r3.xyz;
  r3.xyz = r3.xyz + r8.xyz;
  r3.xyz = -r2.xyz * float3(4,4,4) + r3.xyz;
  r0.yzw = r3.xyz * r0.yyy + r2.xyz;
  r1.zw = cb0[58].zw * v0.xy + cb0[59].xy;
  r1.zw = max(cb0[50].zw, r1.zw);
  r1.zw = min(cb0[51].xy, r1.zw);
  r2.xyz = t1.Sample(s1_s, r1.zw).xyz;
  r2.xyz = cb1[135].zzz * r2.xyz;
  r1.xy = r1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r1.xyz = t2.Sample(s2_s, r1.xy).xyz;
  r1.xyz = r1.xyz * cb0[66].xyz + cb0[61].xyz;
  r1.xyz = r2.xyz * r1.xyz;
  r0.yzw = r0.yzw * cb0[60].xyz + r1.xyz;
  r0.yzw = v1.x * r0.yzw;
  r1.xy = cb0[62].xx * v1.yz;
  r1.x = dot(r1.xy, r1.xy);
  r1.x = 1 + r1.x;
  r1.x = rcp(r1.x);
  r1.x = r1.x * r1.x;
  r0.yzw = r1.x * r0.yzw;
  r1.x = r0.x * cb0[64].x + cb0[64].y;
  r0.yzw = r0.yzw * r1.x + float3(0.00266771927,0.00266771927,0.00266771927);
  r0.yzw = log2(r0.yzw);
  r0.yzw = saturate(r0.yzw * float3(0.0714285746,0.0714285746,0.0714285746) + float3(0.610726953,0.610726953,0.610726953));
  r0.yzw = r0.yzw * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  r0.yzw = t3.Sample(s3_s, r0.yzw).xyz;
  r1.xyz = r0.yzw * 1.05;

  o0.xyz = r1.xyz;
  return;

  o0.w = dot(r1.xyz, float3(0.298999995,0.587000012,0.114));
  r0.x = r0.x * 0.00390625 + -0.001953125;
  r0.xyz = r0.yzw * float3(1.04999995,1.04999995,1.04999995) + r0.x;

  if (cb0[65].x != 0) {
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
    r1.xyz = r1.xyz / cb0[64].www;
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
  }
}