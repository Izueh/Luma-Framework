Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float2 v0 : TEXCOORD0,
  float4 v1 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.x = t0.Sample(s0_s, v0.xy).x;
  o0.w = v1.w * r0.x;
  o0.xyz = v1.xyz;
  
  // Luma: fix UI negative values to emulate UNORM blends
  o0.w = saturate(o0.w);
  o0.xyz = max(o0.xyz, 0.f);
}