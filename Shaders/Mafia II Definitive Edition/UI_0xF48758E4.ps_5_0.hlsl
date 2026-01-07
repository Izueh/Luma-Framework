cbuffer _Globals : register(b0)
{
  float4 color : packoffset(c0);
}

SamplerState Tex_sampler_s : register(s0);
Texture2D<float4> Tex : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = Tex.Sample(Tex_sampler_s, v1.xy).xyzw;
  o0.xyzw = color.xyzw * r0.xyzw;

#if 0 // TODO: verify it won't pollute any other render targets. Not needed until proven otherwise.
  o0.a = saturate(o0.a); // Luma: emulate UNORM render targets
#endif
}