// Luma: ps_5_0 Luma_DrawPurple_PS COLOR=float4(1.0,0.0,1.0,1.0)
// Luma: ps_5_0 Luma_DrawNaN_PS COLOR=FLT_NAN
#include "Includes/Core/Common.hlsl"

#ifndef COLOR
#define COLOR float4(0, 0, 0, 0)
#endif

float4 main() : SV_Target
{
  return COLOR;
}