#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 uv1 = v1.xy;
  float2 uv2 = v1.zw;
  
  bool isFullscreenVideo = LumaData.CustomData1 != 0; // The texture is swapchain sized, so we need to consider the aspect ratio to avoid stretching
  bool isMainMenu = LumaData.CustomData1 >= 2;
  if (isFullscreenVideo)
  {
    // Luma: fix videos being stretched. They were drawn on a buffer matching your swapchain resolution (and aspect ratio), instead of 16:9 matched version of it
    float width, height;
    t0.GetDimensions(width, height);
    float sourceAspectRatio = width / height;
    float targetAspectRatio = LumaSettings.SwapchainSize.x / LumaSettings.SwapchainSize.y;

    float2 scale = 1.0;

    if (targetAspectRatio >= sourceAspectRatio)
      scale.x = targetAspectRatio / sourceAspectRatio;
    else
      scale.y = sourceAspectRatio / targetAspectRatio;
      
    // Center the UVs before scaling them
    uv1 = (uv1 - 0.5) * scale + 0.5;
    uv2 = (uv2 - 0.5) * scale + 0.5;

    // Mirror+Loop the main menu background video, otherwise the non written part stays black and the UI is very hard to read, as it's rendered on top of black, while it was designed to be on white
    if (isMainMenu)
    {
      uv1 = MirrorUV(uv1);
      uv2 = MirrorUV(uv2);
    }

    if ((any(uv1.xy < 0) || any(uv1.xy > 1)) && (any(uv2.xy < 0) || any(uv2.xy > 1)))
    {
      o0 = float4(0, 0, 0, 1); // Out of bounds UVs, draw black
      return;
    }
  }

#if 1 // Luma: fixed videos having BT.601 coeffs (from the matrix above)
  // Note: the samplers are not clamped, so make sure the UVs are saturated!
  float Y = t0.Sample(s0_s, uv1).x; // Note: this can be double resolution than the other two. // Note: sometimes (e.g. in the main menu background video), the last vertical line of this is glitched (e.g. white)
  float Cb = t2.Sample(s0_s, uv2).x;
  float Cr = t1.Sample(s0_s, uv2).x;
#if DEVELOPMENT
  o0.xyz = YUVtoRGB(Y, Cr, Cb, DVS1 * 4);
#else
  o0.xyz = YUVtoRGB(Y, Cr, Cb, 1); // TODO: make sure it's right and also that this isn't used for 3d scene textures
#endif
  o0.w = 1.0; // The w value was seemengly random
#else
  float4 r0,r1;
  r0.xyzw = t1.Sample(s0_s, uv2).xyzw;
  r0.xyzw = cb0[1].xyzw * r0.x;
  r1.xyzw = t0.Sample(s0_s, uv1).xyzw;
  r0.xyzw = r1.x * cb0[4].xyzw + r0.xyzw;
  r1.xyzw = t2.Sample(s0_s, uv2).xyzw;
  r0.xyzw = r1.x * cb0[2].xyzw + r0.xyzw;
  r0.xyzw = cb0[3].xyzw + r0.xyzw;
  o0.xyzw = cb0[0].xyzw * r0.xyzw;
#endif
  
  if (isFullscreenVideo)
  {
#if ENABLE_HDR_BOOST // HDR upgrade works here because we upgrade R8G8B8A8_UNORM to float, but otherwise we could do it during the playback too
    o0.rgb = gamma_to_linear(o0.rgb, GCT_MIRROR);
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
    o0.rgb = linear_to_gamma(o0.rgb, GCT_MIRROR);
#endif

#if UI_DRAW_TYPE == 2 // This is drawn in the UI phase but it's not exactly classifiable UI, so make sure it scales with the game brightness instead
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif
  }
}