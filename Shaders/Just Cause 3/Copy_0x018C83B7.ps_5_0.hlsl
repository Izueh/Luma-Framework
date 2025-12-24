#include "Includes/Common.hlsl"

SamplerState Texture0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyz = Texture0.Sample(Texture0_s, v1.xy).xyz;
  o0.w = 1;
  
  if (LumaData.CustomData1) // Make sure this is a pause menu background copy
  {
    // If SR is active, we would have previously converted to BT.2020 to avoid SR clipping negative scRGB colors!
    if (LumaSettings.SRType)
    {
      o0.rgb = BT2020_To_BT709(o0.rgb);
    }
  }
}