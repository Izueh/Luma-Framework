Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);

cbuffer cb0 : register(b0)
{
  float4 cb0[17];
}
cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

#define cmp

// Controls the intensity of blurring
void main(
  float2 v0 : TEXCOORD0,
  float2 w0 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = t1.SampleLevel(s1_s, v0.xy, 0).x;
  r0.y = 1 + -cb2[2].y;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r0.x = -cb2[2].x / r0.x;
  r1.x = cb0[15].x + cb0[15].y;
  r1.y = cb0[15].x + -cb0[15].y;
  r0.yz = -r1.xy + r0.xx;
  r0.x = cmp(r0.x >= cb0[15].x);
  r0.xw = r0.xx ? float2(-0.5,0) : float2(0,0.5);
  r1.xy = float2(1,-1) * cb0[15].ww;
  r0.yz = saturate(r1.xy * r0.yz);
  r1.x = t2.Sample(s0_s, v0.xy).w;
  r1.x = -1 + r1.x;
  r1.x = cb0[16].x * r1.x + 1;
  r1.y = t0.Sample(s2_s, w0.xy).w;
  r0.yz = saturate(r0.yz * r1.xx + r1.yy);
  r0.x = dot(r0.xw, r0.yz);
  o0.w = 0.5 + r0.x;
  o0.xyz = float3(0,0,0);
}