Texture2D<float4> t7 : register(t7);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s7_s : register(s7);
SamplerState s6_s : register(s6);
SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r0.xyz = r0.www * r0.xyz;
  r0.xyz = float3(4,4,4) * r0.xyz;
  r1.xyz = r0.xyz * r0.xyz;
  r0.w = t1.Sample(s1_s, float2(0.5,0.5)).y;
  r0.w = max(cb0[0].x, r0.w);
  r2.xy = uint2(cb0[1].xy) >> 1;
  r3.xyzw = 1.0 / r2.xyxy;
  r2.xy = v1.xy * r2.xy + float2(-0.5,-0.5);
  r2.xy = frac(r2.xy);
  r4.xyzw = r3.zwzw * float4(-0.5,-0.5,0.5,-0.5) + v1.xyxy;
  r5.xyzw = t2.Sample(s2_s, r4.xy).xyzw;
  r5.xyz = r5.www * r5.xyz;
  r5.xyz = float3(4,4,4) * r5.xyz;
  r5.xyz = r5.xyz * r5.xyz;
  r6.xyzw = t2.Sample(s2_s, r4.zw).xyzw;
  r6.xyz = r6.www * r6.xyz;
  r6.xyz = float3(4,4,4) * r6.xyz;
  r3.xyzw = r3.xyzw * float4(-0.5,0.5,0.5,0.5) + v1.xyxy;
  r7.xyzw = t2.Sample(s2_s, r3.xy).xyzw;
  r7.xyz = r7.www * r7.xyz;
  r7.xyz = float3(4,4,4) * r7.xyz;
  r7.xyz = r7.xyz * r7.xyz;
  r8.xyzw = t2.Sample(s2_s, r3.zw).xyzw;
  r8.xyz = r8.www * r8.xyz;
  r8.xyz = float3(4,4,4) * r8.xyz;
  r6.xyz = r6.xyz * r6.xyz + -r5.xyz;
  r5.xyz = r2.xxx * r6.xyz + r5.xyz;
  r6.xyz = r8.xyz * r8.xyz + -r7.xyz;
  r6.xyz = r2.xxx * r6.xyz + r7.xyz;
  r6.xyz = r6.xyz + -r5.xyz;
  r5.xyz = r2.yyy * r6.xyz + r5.xyz;
  r1.w = t3.Sample(s3_s, v1.xy).x;
  r2.z = t5.Sample(s5_s, v1.xy).x;
  r2.z = r2.z * 2 + cb0[4].x;
  r2.z = -1 + r2.z;
  r2.z = cb0[4].y / r2.z;
  r2.z = saturate(cb0[3].w * r2.z);
  r2.w = -cb0[3].y + r2.z;
  r5.w = cb0[3].x + -cb0[3].y;
  r2.w = r2.w / r5.w;
  r2.z = cmp(cb0[3].y < r2.z);
  r2.z = r2.z ? 1.000000 : 0;
  r1.w = r2.z * r2.w + r1.w;
  r1.w = saturate(cb0[3].z + r1.w);
  r2.z = cb0[2].w * r1.w;
  r6.xy = t6.Sample(s6_s, v1.xy).zw;
  r6.xy = r6.xy * float2(2,2) + float2(-1,-1);
  r6.y = dot(r6.xy, r6.xy);
  r2.w = cmp(cb0[2].z < 0);
  if (r2.w != 0) {
    r6.zw = t7.Sample(s7_s, v1.xy).zw;
    r6.zw = r6.zw * float2(2,2) + float2(-1,-1);
    r2.w = dot(r6.zw, r6.zw);
    r5.w = cmp(r6.y < r2.w);
    r6.y = r5.w ? r2.w : r6.y;
    r6.x = abs(cb0[2].z);
  } else {
    r6.x = cb0[2].z;
  }
  r2.w = saturate(r6.y * r6.x);
  r6.xyz = t4.Sample(s4_s, r4.xy).xyz;
  r4.xyz = t4.Sample(s4_s, r4.zw).xyz;
  r7.xyz = t4.Sample(s4_s, r3.xy).xyz;
  r3.xyz = t4.Sample(s4_s, r3.zw).xyz;
  r4.xyz = r4.xyz + -r6.xyz;
  r4.xyz = r2.xxx * r4.xyz + r6.xyz;
  r3.xyz = r3.xyz + -r7.xyz;
  r3.xyz = r2.xxx * r3.xyz + r7.xyz;
  r3.xyz = r3.xyz + -r4.xyz;
  r3.xyz = r2.yyy * r3.xyz + r4.xyz;
  r1.w = r1.w * cb0[2].w + r2.w;
  r1.w = cmp(0.0500000007 < r1.w);
  r0.xyz = -r0.xyz * r0.xyz + r3.xyz;
  r0.xyz = r2.www * r0.xyz + r1.xyz;
  r2.xyw = r3.xyz + -r0.xyz;
  r0.xyz = r2.zzz * r2.xyw + r0.xyz;
  r0.xyz = r0.xyz * r0.www + r5.xyz;
  r1.xyz = r1.xyz * r0.www + r5.xyz;
  r0.xyz = r1.www ? r0.xyz : r1.xyz;
  r1.xyz = r0.xyz + r0.xyz;
  r2.xyz = r0.xyz * float3(0.300000012,0.300000012,0.300000012) + float3(0.0500000007,0.0500000007,0.0500000007);
  r2.xyz = r1.xyz * r2.xyz + float3(0.00400000019,0.00400000019,0.00400000019);
  r0.xyz = r0.xyz * float3(0.300000012,0.300000012,0.300000012) + float3(0.5,0.5,0.5);
  r0.xyz = r1.xyz * r0.xyz + float3(0.0600000024,0.0600000024,0.0600000024);
  r0.xyz = r2.xyz / r0.xyz;
  r0.xyz = float3(-0.0666666627,-0.0666666627,-0.0666666627) + r0.xyz;
  r0.xyz = float3(1.37906432,1.37906432,1.37906432) * r0.xyz;
  o0.xyz = sqrt(abs(r0.xyz)) * sign(r0.xyz); // Luma: made sqrt safe
  o0.w = 1;
}