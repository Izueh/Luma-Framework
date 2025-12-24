SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyzw;
}