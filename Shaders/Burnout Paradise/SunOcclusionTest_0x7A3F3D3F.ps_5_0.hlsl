#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 kUvStartAndOffset : packoffset(c0);
}

SamplerState SamplerSource_s : register(s0);
Texture2D<float> SamplerSourceTexture : register(t0); // Luma: fixed this from being accidentally set to the scene color texture instead of the depth if MSAA was on

// Does 7x7 samples to find how many of the depth values are >= 1 (sky), to tell how bright the sun bloom should be.
// This PS draws on the whole 1x1 range of the render target, so the sampling UVs of the depth are exclusively determined by cbuffers.
void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  const int originalIterations = 7;
#if 1 // Luma: increase quality to make it more granular (beyond a certain amount it doesn't help, it still flickers, it'd need to be termporal (which we did, it now blends))
  const int iterations = 12;
#else
  const int iterations = originalIterations;
#endif

  float4 uvStartAndOffset = kUvStartAndOffset;
#if 1 // Luma: fix test range being too small
#if 0 // Luma: fix sun occlusion test range being stretched in UW. Disabled as this is actually already handled!
  float targetAspectRatio = LumaSettings.GameSettings.InvRenderRes.y / LumaSettings.GameSettings.InvRenderRes.x;
  float aspectRatioCorrection = min((16.0 / 9.0) / targetAspectRatio, 1.0); // Leave 4:3 untouched given I couldn't test it, it might already be handled
#else
  float aspectRatioCorrection = 1.0;
#endif

  float horizontalRangeScaling = aspectRatioCorrection;
#if 1 // If we wanted to cover a wider area to make it more stable, we can do this, given the default range is like 3 pixels at the center...
  horizontalRangeScaling *= 8.0;
#endif

  // Reconstruct the horizontal center (the sun position in screen UV), and then unstretch it based on the aspect ratio 
  float startX = uvStartAndOffset.x;
  float endX = startX + uvStartAndOffset.z * (originalIterations - 1);
  float centerX = lerp(startX, endX, 0.5);
  float adjustedStartX = lerp(centerX, startX, horizontalRangeScaling);
  uvStartAndOffset.x = adjustedStartX;
  uvStartAndOffset.z *= horizontalRangeScaling;
#endif
  uvStartAndOffset.zw *= float(originalIterations) / float(iterations); // Scale the iterations offsets by new new iterations count to end up in the same place at the end (cover the same range)

  float depthSum = 0.0;
  float startOffset = (iterations - 1) * 0.5; // Luma: changed this to support both even and uneven iteration counts
  float2 uv = float2(uvStartAndOffset.x, uvStartAndOffset.y);
  uv.y -= uvStartAndOffset.w * startOffset; // The Y position is based around the center, and the offsets covers both above and below
  int i = 0;
  while (i < iterations) {
    int k = 0;
    while (k < iterations) {
      float depth = SamplerSourceTexture.Sample(SamplerSource_s, uv).r;
      depthSum += (depth < 1) ? 0 : 1; // Is sky (cleared depth)? No need for thresholds
      uv.x += uvStartAndOffset.z;
      k++;
    }
    uv.y += uvStartAndOffset.w;
    uv.x = uvStartAndOffset.x; // Reset x at every loop. The x start is based on the left and iterations go towards the right (no centering like for Y)
    i++;
  }
  o0.xyz = depthSum / float(iterations * iterations); // Output is 1x1 R8G8B8A8_UNORM, even if only R is ever read back
  o0.w = 1;
}