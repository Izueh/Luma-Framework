Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = t0.Sample(s0_s, v0.xy).xyzw;
}