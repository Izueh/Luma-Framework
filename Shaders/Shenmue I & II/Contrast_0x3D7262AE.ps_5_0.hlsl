cbuffer CB_Contrast : register(b4)
{
  float contrast : packoffset(c0);
}

// Uses a blend mode to draw additively on the render target
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyz = contrast;
  o0.w = 1;
}