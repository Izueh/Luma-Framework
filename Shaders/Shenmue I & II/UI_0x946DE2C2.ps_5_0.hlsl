SamplerState sampler0_s : register(s0);
Texture2D<float4> texture0 : register(t8);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = texture0.Sample(sampler0_s, v2.xy).xyzw;
  o0.xyzw = v1.xyzw * r0.xyzw;

#if 1 // Luma: emulate UNORM, just in case
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
#endif
}