#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 c130_GlobalSceneParams : packoffset(c15);
  float2 D013_SpecularPowerAndLevel : packoffset(c64);
  float D350_ForcedWorldNormalZ : packoffset(c65);
  float4 c025_VisualColorModulator : packoffset(c99);
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = c025_VisualColorModulator.xyzw * v1.xyzw;
  r0.xyzw = max(0.0, r0.xyzw);
#if 1 // Luma: make sure alpha is clamped to emulate the original UNORM behaviour (the game was ALL UNORM)
  o0.w = saturate(r0.w);
#else
  o0.w = min(1, r0.w);
#endif
  o0.xyz = r0.xyz;

#if 1 // Luma: disable black bars (cbuffer flag to tell if this draw matched the conditions black bars draw in)
  if (LumaData.CustomData1 != 0 && all(c025_VisualColorModulator.xyz == 1.0) && c025_VisualColorModulator.w == 1.0) // Also check the color as extra safety
  {
    discard;
  }
#endif
}