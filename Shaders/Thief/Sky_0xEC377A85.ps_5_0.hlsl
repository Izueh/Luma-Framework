#include "../Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[36];
}

// Luma
cbuffer cb2 : register(b2)
{
  float4 cb2[4];
}

void main(
  float4 v0 : COLOR0,
  float3 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD8,
  float2 v4 : TEXCOORD9,
  float4 pixelPos : SV_POSITION0,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2,r3,r4;
  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.y = -v3.z * r0.x + -0.600000024;
  r1.xyz = v3.xyz * r0.xxx;
  r0.x = -v3.z * r0.x + cb0[35].y;
  r0.yz = -r1.xy / r0.yy;
  r0.w = 0.100000001 * cb0[35].x;
  r2.xy = cb0[23].zw * r0.ww;
  r2.xy = r0.yz * cb0[23].xy + r2.xy;
  r2.xyz = t0.Sample(s0_s, r2.xy).xyz;
  r2.xyz = cb0[24].xyz * r2.xyz;
  r3.xy = cb0[25].zw * r0.ww;
  r3.zw = cb0[27].zw * r0.ww;
  r3.zw = r0.yz * cb0[27].xy + r3.zw;
  r0.yz = r0.yz * cb0[25].xy + r3.xy;
  r4.xyzw = t1.Sample(s1_s, r0.yz).xyzw;
  r3.xyzw = t2.Sample(s2_s, r3.zw).xyzw;
  r0.yzw = r4.xyz * cb0[26].xyz + -r2.xyz;
  r1.w = r4.w * r4.w;
  r0.yzw = r1.www * r0.yzw + r2.xyz;
  r2.xyz = r3.xyz * cb0[28].xyz + -r0.yzw;
  r0.yzw = r3.www * r2.xyz + r0.yzw;
  r1.w = (r0.x < 0);
  r0.x = cb0[30].x * r0.x;
  r0.x = 1 + -abs(r0.x);
  r0.x = saturate(cb0[30].y * r0.x);
  r0.x = r0.x * r0.x;
  r0.yzw = r1.www ? r0.yzw : cb0[29].xyz;
  r2.xyz = cb0[29].xyz + -r0.yzw;
  r0.xyz = r0.xxx * r2.xyz + r0.yzw;
  r0.w = dot(cb0[31].xyz, cb0[31].xyz);
  r0.w = sqrt(r0.w);
  r2.xyz = cb0[31].xyz / r0.www;
  r0.w = dot(r2.xyz, r1.xyz);
  r1.xy = cb0[35].zw + r0.ww;
  r1.zw = float2(1,1) + cb0[35].zw;
  r1.xy = saturate(r1.xy / r1.zw);
  r1.xz = r1.xy * r1.xy;
  r0.w = r1.z * r1.y;
  r1.x = r1.x * r1.x;
  r1.yzw = cb0[33].www * cb0[33].xyz;
  r1.yzw = r0.www * r1.yzw + cb0[34].xyz;
  r2.xyz = cb0[32].www * cb0[32].xyz;
  r1.xyz = r1.xxx * r2.xyz + r1.yzw;
  o0.xyz = r0.xyz * r1.xyz + cb0[22].xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);

// TODO: this is the day sky? Should we actually boost it?
#if ENABLE_FAKE_HDR // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that
  int superSampling = 2;
  bool forceVanillaSDR = ShouldForceSDR(pixelPos.xy / (cb2[3].xy * superSampling));
  if (LumaSettings.DisplayMode == 1 && !forceVanillaSDR)
  {
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.25;
    float saturationBoost = 0.75;
    o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity, saturationBoost);
  }
#endif
}