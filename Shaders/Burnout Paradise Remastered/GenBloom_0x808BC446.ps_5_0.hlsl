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

float3 BloomSample(float2 uv, float2 sourceSize, bool improvedBloom = false, bool forceVanilla = false)
{
  float4 r0;
  float3 sceneColor;
  if (improvedBloom && !forceVanilla) // Luma: better bloom
  {
    int2  base = int2(uv * sourceSize - 0.5);
    float3 c00 = SamplerSourceTexture.Load(int3(base.x,     base.y,     0)).xyz;
    float3 c10 = SamplerSourceTexture.Load(int3(base.x + 1, base.y,     0)).xyz;
    float3 c01 = SamplerSourceTexture.Load(int3(base.x,     base.y + 1, 0)).xyz;
    float3 c11 = SamplerSourceTexture.Load(int3(base.x + 1, base.y + 1, 0)).xyz;
    
#if 0 // Kill fireflies (occasionally this game seems to output very bright single pixels for one frame that expand and cause circular blinks in bloom) // TODO
    float3 cs[4] = { c00, c10, c01, c11 };
    // Use the average as we are in gamma space here and anyway the luminance is meaningless
    float4 lums  = float4(
        average(cs[0]),
        average(cs[1]),
        average(cs[2]),
        average(cs[3])
    );

    // Find brightest sample
    int maxIdx = 0;
    if (lums.y > lums[maxIdx]) maxIdx = 1;
    if (lums.z > lums[maxIdx]) maxIdx = 2;
    if (lums.w > lums[maxIdx]) maxIdx = 3;
    
    float maxLum = lums[maxIdx];

    // Compute avg luminance of remaining 3 (for outlier test)
    float avgLum3 = 0.0;
    float3 avgC3 = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (i == maxIdx) continue;
        avgLum3 += lums[i];
        avgC3 += cs[i];
    }
    avgLum3 /= 3.0;

    // If brightest is significantly brighter than the rest, drop it
    const float fireflyThreshold = 3.0;
    bool isOutlier = (maxLum > avgLum3 * fireflyThreshold);
    if (isOutlier) // TODO: try with a lerp instead of a hard branch. Also maybe try calculating it on 16 pixels to avoid more false positives and avoid clamping the edge of lights.
    {
#if 0
      // Nothing to do in this case, completely drop the outlier sample
      sceneColor = avgC3 / 3.0;
#else
      // Get min/max luminance of the 3 "reasonable" samples
      float minLum = FLT_MAX, maxLum = -FLT_MAX;

      [unroll]
      for (int i = 0; i < 4; ++i)
      {
        if (i == maxIdx) continue;
        minLum = min(minLum, lums[i]);
        maxLum = max(maxLum, lums[i]);
      }

      // Clamp outlier to min/max range from the other 3 samples
      float clampedLum = clamp(lums[maxIdx], minLum, maxLum);
      cs[maxIdx] *= (lums[maxIdx] != 0.0) ? (clampedLum / lums[maxIdx]) : 0.0;

      sceneColor = (avgC3 + cs[maxIdx]) / 4.0;
#endif
    }
    else
    {
      sceneColor = (avgC3 + cs[maxIdx]) / 4.0;
    }
#else
    sceneColor = (c00 + c10 + c01 + c11) / 4.0;
#endif
  }
  else
  {
    sceneColor = SamplerSourceTexture.Sample(SamplerSource_s, uv).xyz;
  }
  sceneColor = max(sceneColor, -FLT16_MAX); // Luma: strip away nans (probably not necessary as they'd get clipped later)
  sceneColor = IsInfinite_Strict(sceneColor) ? 0.0 : sceneColor; // Luma: clamp infinite (we can't have -INF as we previous clip all negative values from materials rendering) (this seems to fix white dots that become white blobs for one pixel sometimes, likely due to bloom)
  
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

  sum += BloomSample(v1.xy, sourceSize, improvedBloom, forceVanilla);
  sum += BloomSample(v1.zw, sourceSize, improvedBloom, forceVanilla);
  sum += BloomSample(v2.xy, sourceSize, improvedBloom, forceVanilla);
  sum += BloomSample(v2.zw, sourceSize, improvedBloom, forceVanilla);

  o0.xyz = kThresholdAndScale.y * sum; // Likely 1/4 (samples count)
  o0.w = 1;
  
  // Clamp bloom before smoothing it in the next passes
  if (forceVanilla)
  {
    o0.xyz = saturate(o0.xyz);
  }
}