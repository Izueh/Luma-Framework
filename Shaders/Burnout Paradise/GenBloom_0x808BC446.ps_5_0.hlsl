#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float3 kDotWithWhiteLevel : packoffset(c0);
  float3 kThresholdAndScale : packoffset(c1);
}

SamplerState SamplerSource_s : register(s0);
Texture2D<float4> SamplerSourceTexture : register(t0);

// Luma: removed saturate
float3 OptionalSaturate(float3 x, bool forceVanilla = false)
{
  return forceVanilla ? saturate(x) : x;
}

float3 BloomSample(float2 uv, bool improvedBloom = false, bool forceVanilla = false)
{
  float4 r0;
  float3 sceneColor = SamplerSourceTexture.Sample(SamplerSource_s, uv).xyz;
  sceneColor = max(sceneColor, -FLT16_MAX); // Luma: strip away nans (probably not necessary as they'd get clipped later)
  sceneColor = IsInfinite_Strict(sceneColor) ? 1.0 : sceneColor; // Luma: clamp infinite (we can't have -INF as we previous clip all negative values from materials rendering) (this seems to fix white dots that become white blobs for one pixel sometimes, likely due to bloom)
  if (improvedBloom && !forceVanilla) // Luma: better bloom
  {
    float3 normalizedSceneColor = sceneColor / max(max3(sceneColor), 1.0);
    r0.w = dot(normalizedSceneColor, kDotWithWhiteLevel.xyz);
    // Instead of subtracting a fixed amount and causing most of the image to go black, make bloom exponentially brighter as brightness increases
    float thresholdAnchorPoint = kThresholdAndScale.x * 1.5;
    float thresholdScalingRatio = sqrt(saturate(r0.w / thresholdAnchorPoint)); // We can do a sqrt here to bring it even closer to vanilla, but then it'd kinda crush colors
    r0.w -= kThresholdAndScale.x * thresholdScalingRatio;
  }
  else
  {
    r0.w = dot(saturate(sceneColor), kDotWithWhiteLevel.xyz); // Luma: added saturate to these or they'd go crazy (it also emulates the vanilla result more accurately)
    r0.w -= kThresholdAndScale.x;
  }
  r0.w = max(r0.w, 0.0); // Luma: added this as otherwise it goes negative and flips the color
  return OptionalSaturate(sceneColor * r0.w, forceVanilla);
}

// Downscales from 1 to 0.25 size
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float3 sum = 0;
  
  float2 sourceSize;
  SamplerSourceTexture.GetDimensions(sourceSize.x, sourceSize.y);
  float2 uv = v0.xy / (float2(sourceSize.x, sourceSize.y) / 4.0);
  bool forceVanilla = ShouldForceSDR(uv);

  bool improvedBloom = false;
#if ENABLE_IMPROVED_BLOOM
  improvedBloom = true;
#endif // ENABLE_IMPROVED_BLOOM

  sum += BloomSample(v1.xy, improvedBloom, forceVanilla);
  sum += BloomSample(v1.zw, improvedBloom, forceVanilla);
  sum += BloomSample(v2.xy, improvedBloom, forceVanilla);
  sum += BloomSample(v2.zw, improvedBloom, forceVanilla);

  o0.xyz = kThresholdAndScale.y * sum; // Likely 1/4 (samples count)
  o0.w = 1;
  
  // Clamp bloom before smoothing it in the next passes
  if (forceVanilla)
  {
    o0.xyz = saturate(o0.xyz);
  }
}