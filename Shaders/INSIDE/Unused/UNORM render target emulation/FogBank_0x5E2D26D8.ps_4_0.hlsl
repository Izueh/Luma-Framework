Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[6];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[21];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = v3.xy / v3.ww;
  r0.xyzw = t2.SampleLevel(s0_s, r0.xy, 0).xyzw;
  r1.xyzw = t0.Sample(s2_s, v2.xy).xyzw;
  r2.xyzw = t0.Sample(s2_s, v2.zw).xyzw;
  r0.yzw = r2.xyz + r1.xyz;
  r0.w = r0.w * cb0[19].w + v3.z;
  r0.yz = float2(-1,-1) + r0.yz;
  r0.x = cb1[5].z * r0.x + -r0.w;
  r0.x = saturate(r0.x / cb0[19].z);
  r0.w = r0.x * r0.x;
  r0.x = -r0.x * 2 + 3;
  r0.x = r0.w * r0.x;
  r0.x = cb0[16].w * r0.x;
  r1.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r0.w = dot(r1.xy, r1.xy);
  r0.w = r0.w * -4 + 4;
  r0.w = max(0, r0.w);
  r0.w = cb0[18].z * r0.w;
  r0.yz = r0.yz * r0.ww + v1.xy;
  r2.xyzw = t1.Sample(s1_s, r0.yz).xyzw;
  r0.x = r2.w * r0.x;
  r0.y = saturate(v1.w);
  r0.z = r0.y * r0.y;
  r0.y = -r0.y * 2 + 3;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * r0.y;
  r0.y = -6.28318548 * cb0[18].x;
  sincos(r0.y, r2.x, r3.x);
  r3.y = r2.x;
  r0.y = dot(r1.xy, r3.xy);
  r0.y = saturate(r0.y * 0.5 + 0.5);
  r0.y = cb0[17].w * r0.y;
  r1.xyz = cb0[17].xyz + -cb0[16].xyz;
  r0.yzw = r0.yyy * r1.xyz + cb0[16].xyz;
  r1.xyz = cb2[0].xyz + -r0.yzw;
  r1.w = min(cb2[0].w, v1.z);
  r0.yzw = r1.www * r1.xyz + r0.yzw;
  r1.x = 1 + -r1.w;
  r1.x = -cb0[18].y * r1.x + 1;
  r1.w = r1.x * r0.x;
  r1.xyz = r0.yzw * r0.xxx;
  r0.xy = cb1[1].xx + v3.xy;
  r0.xy = float2(5.39870024,5.44210005) * r0.xy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r2.xyz = float3(0.00392156886,0.00392156886,0.00392156886) * r0.xyz;
  r2.w = r0.w / cb0[20].x;
  o0.xyzw = -r2.xyzw + r1.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0);
  o0.a = saturate(o0.a);
}