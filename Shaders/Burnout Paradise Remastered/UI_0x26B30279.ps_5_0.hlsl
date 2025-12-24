cbuffer _Globals : register(b0)
{
  float4 gvMaskUseFlags : packoffset(c0);
  float4 gvMaskAPositionMinMax : packoffset(c1);
  float4 gvMaskAUVStartEnd : packoffset(c2);
  float4 gvMaskAUVDifference : packoffset(c3);
  float4 gvMaskBPositionMinMax : packoffset(c4);
  float4 gvMaskBUVStartEnd : packoffset(c5);
  float4 gvMaskBUVDifference : packoffset(c6);
  float3 gv3OuterColour : packoffset(c7);
  float3 gv3InnerColour : packoffset(c8);
}

SamplerState DiffuseSampler_s : register(s0);
SamplerState MaskSampler0_s : register(s1);
SamplerState MaskSampler1_s : register(s2);
Texture2D<float4> DiffuseSamplerTexture : register(t0);
Texture2D<float4> MaskSampler0Texture : register(t1);
Texture2D<float4> MaskSampler1Texture : register(t2);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float2 v6 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = max(abs(v5.xz), abs(v5.yw));
  r0.xy = (float2(1,1) >= r0.xy);
  r0.xy = r0.xy ? float2(1,1) : 0;
  r0.z = MaskSampler0Texture.Sample(MaskSampler0_s, v4.xy).w;
  r0.w = MaskSampler1Texture.Sample(MaskSampler1_s, v4.zw).w;
  r0.xy = r0.zw * r0.xy;
  r0.xy = max(v6.xy, r0.xy);
  r0.x = r0.x * r0.y;
  r1.xyw = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v3.xy).xyw;
  r0.yzw = gv3InnerColour.xyz * r1.xxx;
  r1.xyz = r1.yyy * gv3OuterColour.xyz + r0.yzw;
  r1.xyzw = r1.xyzw * v2.xyzw + v1.xyzw;
  o0.w = r1.w * r0.x;
  o0.xyz = r1.xyz;

  // Luma: UNORM RT emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.w = saturate(o0.w);
}