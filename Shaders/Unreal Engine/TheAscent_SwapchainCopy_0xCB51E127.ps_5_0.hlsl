Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[143];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[39];
}

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = asuint(cb0[37].xy);
  r0.xy = v0.xy + -r0.xy;
  r0.xy = cb0[38].zw * r0.xy;
  r0.xy = r0.xy * cb0[5].xy + cb0[4].xy;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.w = -0.8 + cb1[142].z;
  r0.w = saturate(0.6 * r0.w);
  r1.xyz = r0.w * r0.xyz;
  r0.xyz = -r0.w * r0.xyz + cb2[1].xyz;
  r0.xyz = cb2[2].x * r0.xyz + r1.xyz;
#if 1 // Luma
  o0.xyz = r0.xyz;
#else
  o0.xyz = max(0.0, r0.xyz);
#endif
  o0.xyz = r0.xyz;
  o0.w = 1;
}