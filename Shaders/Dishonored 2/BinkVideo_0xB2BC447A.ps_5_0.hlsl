// ---- Created with 3Dmigoto v1.3.16 on Mon Dec 30 15:50:11 2024

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_bink_adj : packoffset(c0);
  float4 cb_bink_yscale : packoffset(c1);
  float4 cb_bink_crc : packoffset(c2);
  float4 cb_bink_cbc : packoffset(c3);
}

SamplerState smp_bilinearsampler_s : register(s0);
Texture2D<float> ro_binkcb : register(t0);
Texture2D<float> ro_binkcr : register(t1);
Texture2D<float> ro_binky : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = ro_binkcr.Sample(smp_bilinearsampler_s, v0.xy).x;
  r0.xyz = cb_bink_crc.xyz * r0.xxx;
  r0.w = ro_binky.Sample(smp_bilinearsampler_s, v0.xy).x;
  r0.xyz = r0.www * cb_bink_yscale.xyz + r0.xyz;
  r0.w = ro_binkcb.Sample(smp_bilinearsampler_s, v0.xy).x;
  r0.xyz = r0.www * cb_bink_cbc.xyz + r0.xyz;
  r0.xyz = cb_bink_adj.xyz + r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = float3(2.20000005,2.20000005,2.20000005) * r0.xyz;
  o0.xyz = exp2(r0.xyz);
  o0.w = 1;
  return;
}