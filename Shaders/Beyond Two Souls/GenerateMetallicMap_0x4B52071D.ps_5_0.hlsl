Texture2D<float4> sceneTexture : register(t0);

SamplerState pointSampler : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  float3 sceneColor = sceneTexture.Sample(pointSampler, v1.xy).rgb;
  sceneColor *= 0.25;
  r0.w = clamp(max(sceneColor.z, max(sceneColor.x, sceneColor.y)), 9.99999997e-007, 1.0);
#if 1 // Luma: disabled stupid quantization // TODO: why does it look worse without!???
  r0.w = ceil(r0.w * 255.0) / 255.0;
#endif
  o0.xyz = sceneColor.xyz / r0.w;
  o0.w = r0.w;
}