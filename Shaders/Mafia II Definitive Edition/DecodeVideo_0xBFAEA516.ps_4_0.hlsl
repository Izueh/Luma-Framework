#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef FORCE_BT709_VIDEOS
#define FORCE_BT709_VIDEOS 1
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

  // Note: the samplers are not clamped, so make sure the UVs are saturated!
  float Y = t0.Sample(s0_s, uv1).x; // Note: this can be double resolution than the other two. // Note: sometimes (e.g. in the main menu background video), the last vertical line of this is glitched (e.g. white)
  float Cb = t2.Sample(s0_s, uv2).x;
  float Cr = t1.Sample(s0_s, uv2).x;

#if FORCE_BT709_VIDEOS
  // TODO: verify at 100% both of these fixes are right... there's something weird about them. Why would the new bink videos still required BT.709 decoding (as opposed to BT.601?) to avoid red skin tones? Maybe they were meant to be like that? I don't see why the bink matrices would be wrong unless the devs hardcoded BT.601 ones in there. Though we pretty much verified it by matching it with our BT.601 matrices!!!
  // The thing is... if you encode a BT.709 video with bink, if it was already YCrCb or YUV, bink might not do any color conversion (e.g. no math that ever acknowledges the color space),
  // it'd just assume BT.601 for encoding and thus it should be decoded using BT.709 to get back the original RGB BT.709 values, even if the meta data assumed it would have been BT.601.
  // If the source video was encoded with RGB instead of YUV. Even if bink acknowledges a BT.601 color space, we should decode it back as BT.601 to get back the original BT.709 RGB values.
  // However the first case above can be proven wrong by encoding a YUV video to bink v1 and playing it back locally.
  // The original video and the bink video will look the same, meaning that either bink did a YUV to YCrCb conversion, converting from BT.709 to BT.601 (makes no sense) on encode and then converting it back to RGB BT.709 with a BT.601 YCrCb matrix on playback,
  // or alternatively encoded without any color conversion and decoded with BT.709. However, if that was the case, why would all games decode bink 1 videos ("old color space") with BT.601? Were they correct? Is the bink video player app different/correct/incorrect?
  // Mafia II DE specifically looks quite red during some rendered scenes too, so the redness on faces might be intentional even in videos.

  // Luma: fixed videos having BT.601 coeffs (from the matrix above)
  // Old bink color space (v1) (BT.601 limited range).
  // The game menu background videos use this, as they had not been re-rendered for the Definitive Edition.
  // Old bink videos forced BT.601 decoding however they were actually encoded with BT.709, the bink encoded simply took a YUV BT.709 video without doing any conversions, assuming it would have been BT.601.
  if (all(abs(cb0[4].xyz - 1.1641235) <= 0.0000001))
  {
    o0.xyz = YUVtoRGB(Y, Cr, Cb, 1); // Note that our version of BT.601 limited range decode seems slightly different than whatever matrix they passed here (though it's not relevant)
    o0.w = 1.0; // The w value was seemengly random
  }
  // Newer videos should theoretically have been decoded using BT.709 full range but they still seem to be using BT.601 full range, so we fix it here.
  else if (all(abs(cb0[4].xyz - 1.0) <= 0.0000001))
  {
    o0.xyz = YUVtoRGB(Y, Cr, Cb, 0); // Note that our version of BT.709 full range decode seems slightly different than whatever matrix they passed here
    o0.w = 1.0;
  }
  // Leave other videos as they were
  else
#endif // FORCE_BT709_VIDEOS
  {
    float4 r0;
    r0.xyzw = 0.0;
    r0.xyzw += Y * cb0[4].xyzw;
    r0.xyzw += Cr * cb0[1].xyzw;
    r0.xyzw += Cb * cb0[2].xyzw;
    r0.xyzw += cb0[3].xyzw;
    o0.xyzw = cb0[0].xyzw * r0.xyzw;
  }
  
  if (isFullscreenVideo)
  {
    o0.rgb = gamma_to_linear(o0.rgb, GCT_MIRROR);

#if ENABLE_HDR_BOOST // HDR upgrade works here because we upgrade R8G8B8A8_UNORM to float, but otherwise we could do it during the playback too
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
#endif

#if UI_DRAW_TYPE == 2 // This is drawn in the UI phase but it's not exactly classifiable UI, so make sure it scales with the game brightness instead
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

    o0.rgb = linear_to_gamma(o0.rgb, GCT_MIRROR);
  }
}