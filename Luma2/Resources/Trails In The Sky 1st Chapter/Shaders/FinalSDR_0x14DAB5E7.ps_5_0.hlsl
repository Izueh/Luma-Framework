// Trails in the Sky 1st Chapter: Final SDR output pass (0x14DAB5E7).
// Vanilla: pow(saturate(tex), gamma), clipping the scene + UI to [0, 1] in gamma space.
// Luma:
//   - Removes the saturate() clip, applies the game's gamma slider, and decodes gamma 2.2 -> linear.
//   - Applies HDR UI scaling (UIPaperWhiteNits / GamePaperWhiteNits), which cancels SceneCopy's pre-scale on the 3D scene
//     while scaling UI elements to UIPaperWhiteNits.
//   - Outputs unscaled linear RGB (1.0 = GamePaperWhiteNits) so Luma Display Composition handles paper white scaling and
//     HDR10 / scRGB / SDR-on-HDR / SDR swapchain encoding.

#include "Game/Common.hlsl"

cbuffer Constants : register(b0)
{
   float gamma : packoffset(c0);
}

SamplerState smpl : register(s0);
Texture2D<float4> tex : register(t0);

static const float GameGamma = 2.2;

void main(
   float4 v0 : SV_Position0,
   float2 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float3 encoded = max(tex.SampleLevel(smpl, v1.xy, 0).xyz, 0.0);

   // Preserve the game's gamma setting without clipping >1 HDR values
   float3 linearColor = pow(encoded, gamma * GameGamma);

   if (LumaSettings.DisplayMode == 1)
   {
      const float gamePaperWhite = max(GamePaperWhiteNits, 1.0);
      const float uiPaperWhite = max(UIPaperWhiteNits, 1.0);
      linearColor *= (uiPaperWhite / gamePaperWhite);
   }
   else
   {
      linearColor = saturate(linearColor);
   }

   o0.xyz = linearColor;
   o0.w = 1.0;
}
