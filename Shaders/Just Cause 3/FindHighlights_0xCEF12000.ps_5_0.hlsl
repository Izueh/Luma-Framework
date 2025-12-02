cbuffer cbConsts : register(b1)
{
  float4 Consts : packoffset(c0);
}

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> ColorTexture : register(t0);

// TODO: this has 2 follow up "downscale" shaders and the last one creates NaNs or black pixels around car reflections from the sun
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float3 o0 : SV_Target0,
  out float o1 : SV_Target1)
{
  float4 r0;
  r0.xyz = ColorTexture.SampleLevel(LinearClampSampler_s, v1.xy, 0).xyz;
#if 1 // Luma
  r0.xyz = max(r0.xyz, 0.0); // Fix Nans and remove negative values (they'd be trash)
#endif
  o0.xyz = r0.xyz;
  r0.x = max(max(r0.x, r0.y), r0.z);
  r0.x = Consts.z * r0.x;
  r0.x = (Consts.w < r0.x);
  o1.x = r0.x ? 1.0 : 0;
}