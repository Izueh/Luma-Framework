cbuffer _Globals : register(b0)
{
  float3 gv3OuterColour : packoffset(c0);
  float3 gv3InnerColour : packoffset(c1);
}

SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> DiffuseSamplerTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyw = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v3.xy).xyw;
  r1.xyz = gv3InnerColour.xyz * r0.xxx;
  r0.xyz = r0.yyy * gv3OuterColour.xyz + r1.xyz;
  o0.xyzw = r0.xyzw * v2.xyzw + v1.xyzw;

  // Luma: UNORM RT emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.w = saturate(o0.w);
}