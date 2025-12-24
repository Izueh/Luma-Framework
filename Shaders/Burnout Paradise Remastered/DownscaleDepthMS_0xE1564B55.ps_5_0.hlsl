cbuffer _Globals : register(b0)
{
  float4 gv4TextureOffsetSize : packoffset(c0);
}

Texture2DMS<float> uSourceSamplerTexture : register(t0);

// 0 min
// 1 max
#define DEPTH_DOWNSCALE_TYPE 1

// Luma: actually take all the depth samples instead of taking the "first" one
float ResolveDepthMS(float2 pixelPos, uint sampleCount)
{
#if 0 // Original non Luma behaviour
  sampleCount = 0;
#endif

  float resolvedDepth = 1.0;
#if DEPTH_DOWNSCALE_TYPE == 1 && 0 // Depth is cleared to 1 but is not inverse in this game, so for MSAA resolving, we need to take the min, otherwise it'd almost always sample 1 on the cleared samples that rasterization didn't overwrite (I think that it only writes some samples, but I'm not 100% sure) (we'd need to resolve the resource to do it properly)
  resolvedDepth = 0.0;
#endif
  for (uint i = 0; i < sampleCount; ++i)
  {
#if DEPTH_DOWNSCALE_TYPE == 1 && 0
    resolvedDepth = max(resolvedDepth, uSourceSamplerTexture.Load(pixelPos, i));
#else
    resolvedDepth = min(resolvedDepth, uSourceSamplerTexture.Load(pixelPos, i));
#endif
  }
  return resolvedDepth;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float o0 : SV_Target0)
{
  float4 r0,r1;
  
  uint width, height, sampleCount;
  uSourceSamplerTexture.GetDimensions(width, height, sampleCount);
  
  r0.xy = gv4TextureOffsetSize.zw * (-gv4TextureOffsetSize.xy + v1.xy);
  float sample1 = ResolveDepthMS(r0.xy, sampleCount);
  r1.xyzw = gv4TextureOffsetSize.zwzw * (gv4TextureOffsetSize.xyxy * float4(1,-1,-1,1) + v1.xyxy);
  float sample2 = ResolveDepthMS(r1.xy, sampleCount);
  float sample3 = ResolveDepthMS(r1.zw, sampleCount);
  r0.zw = gv4TextureOffsetSize.zw * (gv4TextureOffsetSize.xy + v1.xy);
  float sample4 = ResolveDepthMS(r0.zw, sampleCount);
#if DEPTH_DOWNSCALE_TYPE == 1 // The game supposedly took the max depth to better suit SSAO, though this would be worse for particles drawing
  o0.x = max(max(sample1, sample2), max(sample3, sample4));
#else
  o0.x = min(min(sample1, sample2), min(sample3, sample4));
#endif
}