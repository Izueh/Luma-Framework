#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 kUvStartAndOffset : packoffset(c0);
}

SamplerState SamplerSource_s : register(s0);
Texture2D<float4> SamplerSourceTexture : register(t0);

// Does 7x7 samples to find how many of the texels are >= 1
// This is seemengly used to do sun bloom
void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  const int iterations = 7;
  int i = 0;
  float colorSum = 0.0;
  float2 uv = float2(kUvStartAndOffset.x, -kUvStartAndOffset.w * int(iterations / 2) + kUvStartAndOffset.y);
  while (true) {
    if (i >= iterations) break;
    int k = 0;
    while (true) {
      if (k >= iterations) break;

#if 1 // Luma: improve quality with average (this makes it less likely the result will be 1, though given the source is now HDR, it should work!)
      float color = average(SamplerSourceTexture.Sample(SamplerSource_s, uv).rgb);
#else // Originally it only considered the red channel, likely as an approximation (possibly because the sun is red or so)
      float color = SamplerSourceTexture.Sample(SamplerSource_s, uv).r;
#endif
      color = IsNaN_Strict(color) ? 0.0 : color; // Luma: Nans protection (probably not even needed with the code below)
      colorSum += (color < 1) ? 0 : 1;

      uv.x += kUvStartAndOffset.z;
      k++;
    }
    uv.y += kUvStartAndOffset.w;
    i++;
  }
  o0.xyz = colorSum / float(iterations * iterations);
  o0.w = 1;
}