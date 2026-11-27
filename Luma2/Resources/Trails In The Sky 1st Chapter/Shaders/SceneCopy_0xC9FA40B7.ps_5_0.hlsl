// Trails in the Sky 1st Chapter: Scene copy before UI (0xC9FA40B7).
// Vanilla: Plain copy of the finished 3D scene into the buffer UI is subsequently drawn on.
// Luma:
//   - Decodes the finished HDR scene (post-TAA) to linear (1.0 = GamePaperWhiteNits).
//   - Display-maps highlights above paper white to PeakWhiteNits using either:
//       0: Tonemap::CInfinityRollOff (MaxChannel blended with hue-preserving per-channel blowout via MacLeod-Boynton)
//       1: PsychoV 35 (renodx::tonemap::psychov::custom_psychotm_test35) with its built-in dechroma / highlight_saturation blowout
//   - Pre-scales the scene by (GamePaperWhiteNits / UIPaperWhiteNits) before re-encoding to gamma 2.2 so UI blends at [0, 1]
//     and FinalSDR (0x14DAB5E7) can scale both cleanly into linear space for Luma Display Composition.

// clang-format off
#include "Game/Common.hlsl"
#include "Includes/Color/Color.hlsl"
#include "Includes/Color/MacLeodBoynton.hlsl"
#include "Includes/Tonemap/CInfinityRolloff.hlsl"
#include "Game/PsychoV35.hlsli"
// clang-format on

#pragma warning(disable : 4000)

SamplerState smpl : register(s0);
Texture2D<float4> tex : register(t0);

static const float GameGamma = 2.2;

float3 ApplyCInfinityWithBlowout(float3 linearColor, float peakRatio, float anchor, float compression, float blowout)
{
   float3 maxCh = Tonemap::CInfinityRollOff::MaxChannel(linearColor, peakRatio, anchor, compression);

   [branch] if (blowout > 0.001 && max(max(linearColor.x, linearColor.y), linearColor.z) > anchor)
   {
      float3 perCh = Tonemap::CInfinityRollOff::Apply(linearColor, peakRatio.xxx, anchor.xxx, compression);
      // Preserve hue while adopting per-channel desaturation/blowout toward white
      float3 huePreserved2020 = MacLeodBoynton::HueOnlyBT2020(BT709_To_BT2020(perCh), BT709_To_BT2020(maxCh));
      float3 perChHuePreserved = max(BT2020_To_BT709(huePreserved2020), 0.0);
      return lerp(maxCh, perChHuePreserved, saturate(blowout));
   }

   return maxCh;
}

float3 ApplyPsychoV35(float3 linearColor, float peakRatio, float anchor, float compression, float dechroma, float highlightSaturation)
{
   float3 result = renodx::tonemap::psychov::custom_psychotm_test35(
      linearColor,
      peakRatio,
      1.0,                                // exposure
      1.0,                                // highlights
      1.0,                                // shadows
      1.0,                                // contrast
      0.0,                                // flare
      1.0,                                // highlight_contrast
      1.0,                                // shadow_contrast
      1.0,                                // purity_scale
      highlightSaturation,                // highlight_saturation
      dechroma,                           // dechroma (blowout)
      anchor.xxx,                         // current_adaptive_state_bt709
      anchor.xxx,                         // current_background_state_bt709
      0.0,                                // sdr_eotf_emulation
      1.0,                                // gamut_compression
      1,                                  // gamut_compression_mode (1 = BT.2020)
      compression);                       // compression

   return max(result, 0.0);
}

void main(
   float4 v0 : SV_Position0,
   float2 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float3 color = max(tex.SampleLevel(smpl, v1.xy, 0).xyz, 0.0);

   if (LumaSettings.DisplayMode == 1)
   {
      const float gamePaperWhite = max(GamePaperWhiteNits, 1.0);
      const float uiPaperWhite = max(UIPaperWhiteNits, 1.0);
      const float peakRatio = PeakWhiteNits / gamePaperWhite;

      float3 linearColor = pow(color, GameGamma);

      if (peakRatio > 1.01)
      {
         const float anchor = clamp(LumaSettings.GameSettings.rolloff_anchor, 0.18, peakRatio - 0.01);
         const float compression = max(LumaSettings.GameSettings.rolloff_compression, 1.0);

         [branch] if (LumaSettings.GameSettings.tonemap_type >= 0.5)
         {
            linearColor = ApplyPsychoV35(
               linearColor,
               peakRatio,
               anchor,
               compression,
               saturate(LumaSettings.GameSettings.psycho_dechroma),
               max(LumaSettings.GameSettings.psycho_highlight_saturation, 0.0));
         }
         else
         {
            linearColor = ApplyCInfinityWithBlowout(
               linearColor,
               peakRatio,
               anchor,
               compression,
               saturate(LumaSettings.GameSettings.cinfinity_blowout));
         }
      }
      else
      {
         linearColor = saturate(linearColor);
      }

      // Pre-scale to UI paper white space so UI (drawn in [0, 1]) and scene scale independently in FinalSDR (0x14DAB5E7)
      linearColor *= (gamePaperWhite / uiPaperWhite);
      color = pow(max(linearColor, 0.0), 1.0 / GameGamma);
   }
   else
   {
      color = saturate(color);
   }

   o0.xyz = color;
   o0.w = 1.0;
}
