Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[16];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = t1.Sample(s0_s, v1.zw).xyzw;
  r0.x = log2(r0.x);
  r0.x = cb0[14].y * r0.x;
  r0.x = exp2(r0.x);
  r0.x = cb0[14].x * r0.x;
  r0.yzw = float3(0,0,1) + v3.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.y = r0.y + r0.y;
  r0.yz = v3.xy / r0.yy;
  r1.xy = float2(0.5,0.5) + r0.yz;
  r1.z = v1.y;
  r2.xyzw = t3.Sample(s1_s, r1.xz).xyzw;
  r0.yz = r1.xy * float2(0.800000012,0.800000012) + float2(0.100000001,0.100000001);
  r1.xyzw = t2.Sample(s2_s, r0.yz).xyzw;
  r0.x = r2.x * r0.x;
  r0.x = r0.x * r1.x;
  r0.yz = v2.xy / v2.ww;
  r1.xyzw = t0.SampleLevel(s3_s, r0.yz, 0).xyzw;
  r0.y = cb1[5].z * r1.x + -v2.z;
  r0.y = saturate(cb0[15].y * r0.y);
  r0.x = r0.x * r0.y;
  r0.w = v3.w * r0.x;
  r0.xyz = cb0[13].xyz * r0.www;
  r1.xy = cb1[1].xx + v2.xy;
  r1.xy = float2(0.133699998,0.133699998) + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  o0.xyzw = -r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}