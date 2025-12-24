#include "Includes/Common.hlsl"

SamplerState DiffuseSampler_s : register(s0); // Nearest sampler
Texture2D<float4> DiffuseSamplerTexture : register(t0);

// Clears the particles color buffer to black
// and downscales the depth to a lower res one for particles drawing
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float oDepth : SV_Depth)
{
  o0.xyzw = float4(0,0,0,0);
#if 1 // Luma: properly downscale depth for particles instead of taking the nearest
  float2 sourceSize;
  DiffuseSamplerTexture.GetDimensions(sourceSize.x, sourceSize.y);

  uint renderHeight = (1.0 / LumaSettings.GameSettings.InvRenderRes.y) + 0.5;
  uint depthHeight = sourceSize.y + 0.5;
  // If MSAA was off, the depth here would be the one directly from rendering, not previously downscaled (nor MS resolved, given it doesn't need to), as it would in case MSAA was on.
  if (renderHeight == depthHeight)
  {
    float2 halfTexelOffset = 0.5 / sourceSize;
    float depth1 = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy + float2(+halfTexelOffset.x, +halfTexelOffset.y)).x;
    float depth2 = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy + float2(-halfTexelOffset.x, +halfTexelOffset.y)).x;
    float depth3 = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy + float2(+halfTexelOffset.x, -halfTexelOffset.y)).x;
    float depth4 = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy + float2(-halfTexelOffset.x, -halfTexelOffset.y)).x;
    oDepth = min(depth1, min(depth2, min(depth3, depth4)));
  }
  else
  {
    oDepth = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy).x;
  }
#else
  oDepth = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v1.xy).x;
#endif
}