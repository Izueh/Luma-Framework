#include "../Includes/Common.hlsl"

SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  linear noperspective float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyz = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyz;
#if 1 // Luma: improved luminance calcs for FXAA (done in linear and not on BT.601)
  o0.w = linear_to_gamma1(GetLuminance(gamma_to_linear(r0.xyz, GCT_POSITIVE)));
#else
  o0.w = sqrt(dot(r0.xyz * r0.xyz, float3(0.298999995,0.587000012,0.114)));
#endif
  o0.xyz = r0.xyz;
}