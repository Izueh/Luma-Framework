cbuffer cbInstanceConsts : register(b1)
{
  float4 InstanceConsts[2] : packoffset(c0);
}

SamplerState SourceImage_s : register(s0);
Texture2D<float4> SourceImage : register(t0);

// This is run before post processing and runs just like the late auto exposure, though it doesn't seem to do anything
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float oDepth : SV_Depth)
{
  float4 r0;
  r0.xyz = SourceImage.Sample(SourceImage_s, v1.xy).xyz;
#if 1
  r0.xyz = max(r0.xyz, 0.0); // Fix Nans and remove negative values (they'd be trash)
#endif
  r0.xyz = InstanceConsts[0].yyy * r0.xyz;
  r0.x = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
  oDepth = saturate(InstanceConsts[0].x * r0.x);
}