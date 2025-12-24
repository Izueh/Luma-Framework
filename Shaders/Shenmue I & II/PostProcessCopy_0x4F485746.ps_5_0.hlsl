cbuffer ShaderParams : register(b1)
{
  float3 gFogColor : packoffset(c0);
  uint gFogEnabled : packoffset(c0.w);
  float4 gTextureFactor : packoffset(c1);
  uint gAlphaTestEnabled : packoffset(c2);
  float gAlphaRef : packoffset(c2.y);
  uint gAlphaFunc : packoffset(c2.z);
  uint gAlphaOP : packoffset(c2.w);
  uint gAlphaARG0 : packoffset(c3);
  uint gAlphaARG1 : packoffset(c3.y);
  uint gAlphaARG2 : packoffset(c3.z);
  uint gSpecularEnable : packoffset(c3.w);
  uint gRenderMode : packoffset(c4);
  float gDepthBias : packoffset(c4.y);
  uint gUseEnvMap : packoffset(c4.z);
  uint gDisableReflex : packoffset(c4.w);
  float4 Constant0_0 : packoffset(c5);
  float4 Constant0_1 : packoffset(c6);
  float4 Constant0_2 : packoffset(c7);
  float4 Constant0_3 : packoffset(c8);
  float4 Constant0_4 : packoffset(c9);
  float4 Constant0_5 : packoffset(c10);
  float4 Constant0_6 : packoffset(c11);
  float4 Constant0_7 : packoffset(c12);
  float4 Constant1_0 : packoffset(c13);
  float4 Constant1_1 : packoffset(c14);
  float4 Constant1_2 : packoffset(c15);
  float4 Constant1_3 : packoffset(c16);
  float4 Constant1_4 : packoffset(c17);
  float4 Constant1_5 : packoffset(c18);
  float4 Constant1_6 : packoffset(c19);
  float4 Constant1_7 : packoffset(c20);
  float4 c[117] : packoffset(c21);
}

SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> texture0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float2 v2 : TEXCOORD2,
  float2 w2 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  o0.xyz = texture0.Sample(DiffuseSampler_s, v1.xy).xyz;
  o0.w = Constant0_0.w;
}