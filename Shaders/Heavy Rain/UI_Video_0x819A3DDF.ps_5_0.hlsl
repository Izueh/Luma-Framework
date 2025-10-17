#include "Includes/Common.hlsl"

cbuffer ConstentValue : register(b0)
{
  float3 register0 : packoffset(c0);
}

SamplerState sampler0_s : register(s0);
Texture2D<float4> texture0 : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 outColor : SV_TARGET0)
{
  float2 uv = v1.xy * float2(1,-1) + float2(0,1);

  // Luma: fix videos aspect ratio
  float width, height;
  texture0.GetDimensions(width, height);
  float sourceAspectRatio = width / height;
  float targetAspectRatio = LumaSettings.GameSettings.InvRenderRes.y / LumaSettings.GameSettings.InvRenderRes.x;

  float2 scale = 1.0;

  if (targetAspectRatio >= sourceAspectRatio)
    scale.x = targetAspectRatio / sourceAspectRatio;
  else
    scale.y = sourceAspectRatio / targetAspectRatio;

  // Center the UVs before scaling them
  uv = (uv - 0.5) * scale + 0.5;

  float4 videoColor = 0.0;
  if (any(uv.xy < 0) || any(uv.xy > 1))
  {
  }
  else
  {
    uv = saturate(uv); // Be extra sure of not wrapping around as the sampler seems to be wrap, drawing the last line with half of the value from the other edge
    videoColor = texture0.Sample(sampler0_s, uv).rgba;
    videoColor.rgb = pow(abs(videoColor.rgb), register0.xyz) * sign(videoColor.rgb); // Luma: fixed pow going NaN if videos were stored in float textures with negative values
  }
  outColor.rgba = videoColor.rgba;

#if ENABLE_LUMA // Do AutoHDR etc
  outColor.rgb = gamma_to_linear(outColor.rgb, GCT_MIRROR);

  outColor.rgb = PumboAutoHDR(outColor.rgb, lerp(sRGB_WhiteLevelNits, 250.0, LumaSettings.GameSettings.HDRBoostAmount), LumaSettings.GamePaperWhiteNits); // The videos were in terrible quality and have compression artifacts in highlights, so avoid going too high

#if UI_DRAW_TYPE == 2 // Match with scene brightness, not UI brightness
  outColor.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2
  
  outColor.rgb = linear_to_gamma(outColor.rgb, GCT_MIRROR);
#endif
}