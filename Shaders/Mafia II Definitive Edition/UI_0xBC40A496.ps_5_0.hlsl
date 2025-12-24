SamplerState S000_DiffuseTexture_sampler_s : register(s8);
SamplerState S015_DiffuseTexture1_sampler_s : register(s9);
Texture2D<float4> S000_DiffuseTexture : register(t8);
Texture2D<float4> S015_DiffuseTexture1 : register(t9);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.w = S000_DiffuseTexture.Sample(S000_DiffuseTexture_sampler_s, v1.xy).y;
  r1.xyzw = S015_DiffuseTexture1.Sample(S015_DiffuseTexture1_sampler_s, v1.zw).xyzw;
  r0.xyz = float3(1,1,1) + -v3.xyz;
  r0.xyzw = v3.xyzw * r1.xyzw + r0.xyzw;
  o0.xyzw = v2.xyzw * r0.xyzw;

#if 1 // Luma: make sure alpha is clamped to emulate the original UNORM behaviour (the game was ALL UNORM)
  o0.w = saturate(o0.w);
#endif
}