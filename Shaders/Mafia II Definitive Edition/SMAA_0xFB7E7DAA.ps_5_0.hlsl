cbuffer _Globals : register(b0)
{
  float4 SMAA_RT_METRICS : packoffset(c1);
  float4 SMAAParams : packoffset(c2);
  float4 SMAAParams2 : packoffset(c3);
}

SamplerState TMU0_sampler_s : register(s0);
SamplerState TMU1_sampler_s : register(s1);
Texture2D<float4> TMU0 : register(t0);
Texture2D<float4> TMU1 : register(t1);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = TMU1.Sample(TMU1_sampler_s, v2.xy).w;
  r0.y = TMU1.Sample(TMU1_sampler_s, v2.zw).y;
  r0.zw = TMU1.Sample(TMU1_sampler_s, v1.xy).zx;
  r1.x = dot(r0.xyzw, float4(1,1,1,1));
  r1.x = cmp(r1.x < 9.99999975e-006);
  if (r1.x != 0) {
    r1.xyz = TMU0.SampleLevel(TMU0_sampler_s, v1.xy, 0).xyz;
    //r1.xyz = abs(r1.xyz); // Luma: disabled unnecessary abs()
  } else {
    r1.w = max(r0.x, r0.z);
    r2.x = max(r0.y, r0.w);
    r1.w = cmp(r2.x < r1.w);
    r2.xz = r1.ww ? r0.xz : 0;
    r2.yw = r1.ww ? float2(0,0) : r0.yw;
    r0.x = r1.w ? r0.x : r0.y;
    r0.y = r1.w ? r0.z : r0.w;
    r0.z = dot(r0.xy, float2(1,1));
    r0.xy = r0.xy / r0.zz;
    r3.xyzw = SMAA_RT_METRICS.xyxy * float4(1,1,-1,-1);
    r2.xyzw = r2.xyzw * r3.xyzw + v1.xyxy;
    r3.xyz = TMU0.SampleLevel(TMU0_sampler_s, r2.xy, 0).xyz;
    r2.xyz = TMU0.SampleLevel(TMU0_sampler_s, r2.zw, 0).xyz;
    r0.yzw = (r2.xyz) * r0.y; // Luma: disabled unnecessary abs() (and below too)
    r1.xyz = r0.x * (r3.xyz) + r0.yzw;
    r1.xyz = (r1.xyz);
  }
  o0.xyz = r1.xyz;
  o0.w = 0;
}