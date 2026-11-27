// Trails in the Sky 1st Chapter: Bloom / Glow composite (0xD6CF040B).
// Vanilla: 5 sequential Screen blends in gamma space: c += blur_i * max(1.0 - c, 0.0).
// Luma:
//   - Computes the exact clipped SDR vanilla Screen-blend result as the color/hue reference.
//   - Adds true linear-space HDR additive bloom above 1.0 (smoothly matching vanilla SDR bloom energy below 1.0).
//   - Restores the clipped SDR hue and purity (MacLeod-Boynton) onto the HDR luminance so saturated >1 colors (e.g. blue sky)
//     keep the SDR color grading instead of turning dark/oversaturated.

// clang-format off
#include "Game/Common.hlsl"
#include "Includes/Color/Color.hlsl"
#include "Includes/Color/MacLeodBoynton.hlsl"
// clang-format on

#pragma warning(disable : 4000)

cbuffer cb_glow : register(b2)
{
   float4 uv_clamp0_g : packoffset(c0);
   float4 uv_clamp1_g : packoffset(c1);
   float2 uv_clamp2_g : packoffset(c2);
   float2 intensityLum_g : packoffset(c2.z);
   float2 chrIntensityLum_g : packoffset(c3);
   float atmosphereFadeBegin_g : packoffset(c3.z);
   float atmosphereFadeRangeInv_g : packoffset(c3.w);
   float atmosphereIntensity_g : packoffset(c4);
}

SamplerState samLinear : register(s0);
SamplerState samPoint : register(s1);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> blurTexture1 : register(t1);
Texture2D<float4> blurTexture2 : register(t2);
Texture2D<float4> blurTexture3 : register(t3);
Texture2D<float4> blurTexture4 : register(t4);
Texture2D<float4> blurTexture5 : register(t5);

static const float GameGamma = 2.2;

float3 VanillaScreenBlend(float3 base, float3 b1, float3 b2, float3 b3, float3 b4, float3 b5)
{
   float3 c = base;
   c += b1 * max(1.0 - c, 0.0);
   c += b2 * max(1.0 - c, 0.0);
   c += b3 * max(1.0 - c, 0.0);
   c += b4 * max(1.0 - c, 0.0);
   c += b5 * max(1.0 - c, 0.0);
   return c;
}

void main(
   float4 v0 : SV_Position0,
   float4 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float4 baseSample = colorTexture.SampleLevel(samPoint, v1.xy, 0);
   float3 base = max(baseSample.xyz, 0.0);

   float4 uv0 = min(v1.xyxy, uv_clamp0_g);
   float4 uv1 = min(v1.xyxy, uv_clamp1_g);
   float2 uv2 = min(v1.xy, uv_clamp2_g);

   float3 b1 = max(blurTexture1.SampleLevel(samLinear, uv0.xy, 0).xyz, 0.0);
   float3 b2 = max(blurTexture2.SampleLevel(samLinear, uv0.zw, 0).xyz, 0.0);
   float3 b3 = max(blurTexture3.SampleLevel(samLinear, uv1.xy, 0).xyz, 0.0);
   float3 b4 = max(blurTexture4.SampleLevel(samLinear, uv1.zw, 0).xyz, 0.0);
   float3 b5 = max(blurTexture5.SampleLevel(samLinear, uv2.xy, 0).xyz, 0.0);

   o0.w = baseSample.w;

   if (LumaSettings.DisplayMode != 1)
   {
      o0.xyz = VanillaScreenBlend(base, b1, b2, b3, b4, b5);
      return;
   }

   // 1. Exact vanilla clipped SDR reference (clipped base + clipped bloom + Screen blend)
   float3 sdrBase = saturate(base);
   float3 sdrRef = saturate(VanillaScreenBlend(sdrBase, saturate(b1), saturate(b2), saturate(b3), saturate(b4), saturate(b5)));

   // 2. Linear-space HDR additive bloom:
   //    - Preserves exact vanilla SDR bloom energy below 1.0 (sdrBloomLinear)
   //    - Transitions smoothly to physical linear additive bloom for HDR highlights above 1.0
   float3 baseLinear = pow(base, GameGamma);
   float3 sdrBaseLinear = pow(sdrBase, GameGamma);
   float3 sdrRefLinear = pow(sdrRef, GameGamma);
   float3 sdrBloomLinear = max(sdrRefLinear - sdrBaseLinear, 0.0);

   float3 hdrBloomLinear =
      (pow(b1, GameGamma) +
       pow(b2, GameGamma) +
       pow(b3, GameGamma) +
       pow(b4, GameGamma) +
       pow(b5, GameGamma)) * LumaSettings.GameSettings.hdr_bloom_scale;

   float3 hdrLinear = baseLinear + max(sdrBloomLinear, hdrBloomLinear);

   // 3. Restore clipped SDR hue & purity onto HDR luminance when any channel exceeds 1.0
   [branch] if (max(max(hdrLinear.x, hdrLinear.y), hdrLinear.z) > 1.0)
   {
      float3 restored2020 = MacLeodBoynton::HueAndPurityEmulationBT2020(
         BT709_To_BT2020(hdrLinear),
         BT709_To_BT2020(sdrRefLinear),
         LumaSettings.GameSettings.sdr_hue_restore,
         LumaSettings.GameSettings.sdr_purity_restore);
      hdrLinear = max(BT2020_To_BT709(restored2020), 0.0);
   }

   o0.xyz = pow(max(hdrLinear, 0.0), 1.0 / GameGamma);
}
