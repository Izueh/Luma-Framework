Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = cb1[1].xx + v2.xy;
  r0.xy = float2(0.133699998,0.133699998) + r0.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xyzw = t0.Sample(s0_s, v1.zw).xyzw;
  r1.x = log2(r1.x);
  r1.x = cb0[14].y * r1.x;
  r1.x = exp2(r1.x);
  r1.x = cb0[14].x * r1.x;
  r1.yzw = float3(0,0,1) + v3.xyz;
  r1.y = dot(r1.yzw, r1.yzw);
  r1.y = sqrt(r1.y);
  r1.y = r1.y + r1.y;
  r1.yz = v3.xy / r1.yy;
  r2.xy = float2(0.5,0.5) + r1.yz;
  r2.z = v1.y;
  r3.xyzw = t2.Sample(s1_s, r2.xz).xyzw;
  r1.yz = r2.xy * float2(0.800000012,0.800000012) + float2(0.100000001,0.100000001);
  r2.xyzw = t1.Sample(s2_s, r1.yz).xyzw;
  r1.x = r3.x * r1.x;
  r1.x = r1.x * r2.x;
  r1.w = v3.w * r1.x;
  r1.xyz = cb0[13].xyz * r1.www;
  o0.xyzw = -r0.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.00392156886) + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}