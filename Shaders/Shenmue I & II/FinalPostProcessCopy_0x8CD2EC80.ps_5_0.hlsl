#include "../Includes/Common.hlsl"

SamplerState sampler0_s : register(s0);
Texture2D<float4> texture0 : register(t8);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = texture0.Sample(sampler0_s, v1.xy).xyzw;
  
#if UI_DRAW_TYPE == 2
  r0.xyz /= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2

  // Linearize, because the game drew in gamma space and post process is in linear space
  o0.xyz = pow(abs(r0.xyz), 2.2) * sign(r0.xyz); // Luma: added mirroring support
  o0.w = r0.w;
}