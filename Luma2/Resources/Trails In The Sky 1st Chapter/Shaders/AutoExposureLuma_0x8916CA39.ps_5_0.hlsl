// Trails in the Sky 1st Chapter: Auto-exposure luminance extraction (0x8916CA39).
// Luma: Caps luminance fed to the exposure histogram so boosting the HDR sun doesn't over-darken the scene.

#include "Game/Common.hlsl"

SamplerState samLinear : register(s0);
Texture2D<float4> colorTexture : register(t0);

void main(
   float4 v0 : SV_Position0,
   float4 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0)
{
   float3 color = colorTexture.SampleLevel(samLinear, v1.xy, 0).xyz;
   float luma = dot(color, float3(0.298912, 0.586611, 0.114477));

   if (LumaSettings.DisplayMode == 1)
   {
      // Vanilla sun peaked around ~2.8-3.0 in gamma space; cap here so HDR sun boost doesn't shift auto-exposure
      luma = min(luma, 3.0);
   }

   o0 = luma;
}
