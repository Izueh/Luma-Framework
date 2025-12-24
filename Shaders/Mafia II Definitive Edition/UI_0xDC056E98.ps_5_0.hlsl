#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 c130_GlobalSceneParams : packoffset(c15);
  float2 D013_SpecularPowerAndLevel : packoffset(c64);
  float D350_ForcedWorldNormalZ : packoffset(c65);
  float4 c025_VisualColorModulator : packoffset(c99);
}

SamplerState S000_DiffuseTexture_sampler_s : register(s8);
Texture2D<float4> S000_DiffuseTexture : register(t8);

// Also plays videos
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float2 uv = v1.xy;

  // Luma: fix videos being stretched (they stetch only outside of the main menu)
  // Note: in the menu, the shader only drew in the 16:9 portion of the image
  float2 size;
  S000_DiffuseTexture.GetDimensions(size.x, size.y);
  bool isFullscreenVideo = LumaData.CustomData1 != 0;
#if 0 // Not needed anymore, it's now all handled in the video decode shader
  isFullscreenVideo = size.x == LumaSettings.SwapchainSize.x && size.y == LumaSettings.SwapchainSize.y;
  if (isFullscreenVideo)
  {
    float sourceAspectRatio = 16.0 / 9.0; // Assume they all are, given there was no code to handle black bars (beside viewports)
    float targetAspectRatio = LumaSettings.SwapchainSize.x / LumaSettings.SwapchainSize.y;

    float2 scale = 1.0;

    if (targetAspectRatio >= sourceAspectRatio)
      scale.x = targetAspectRatio / sourceAspectRatio;
    else
      scale.y = sourceAspectRatio / targetAspectRatio;

    // Center the UVs before scaling them
    uv = (uv - 0.5) * float2(1.0, 2.0) + 0.5;
  }
#endif

  float4 r0;
  r0.xyzw = S000_DiffuseTexture.Sample(S000_DiffuseTexture_sampler_s, uv).xyzw;
  r0.xyzw = v2.xyzw * r0.xyzw;
  r0.xyzw = c025_VisualColorModulator.xyzw * r0.xyzw;
  if (!isFullscreenVideo) // Leave any possible BT.2020 colors
  {
    r0.xyzw = max(0.0, r0.xyzw);
  }
#if 1 // Luma: saturate alpha just in case!
  o0.w = saturate(r0.w);
#else
  o0.w = min(1, r0.w);
#endif
  o0.xyz = r0.xyz;
}