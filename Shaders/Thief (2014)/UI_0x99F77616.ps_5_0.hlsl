Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : COLOR1,
  float4 v1 : TEXCOORD3,
  float4 v2 : TEXCOORD4,
  float2 v3 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = t0.Sample(s0_s, v3.xy).xyzw;
  r0.xyzw *= v2.xyzw;
  r0.xyzw += v1.xyzw;
  o0.w = v0.w * r0.w;
  o0.xyz = r0.xyz;

  // Luma: fix UI negative values to emulate UNORM blends
  o0.w = saturate(o0.w);
  o0.xyz = max(o0.xyz, 0.f);
}