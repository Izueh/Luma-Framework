#include "Includes/Common.hlsl"

SamplerState sampler0_s : register(s0);
SamplerState sampler1_s : register(s1);
SamplerState sampler2_s : register(s2);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);

// Input and output at video resolution
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 outColor : SV_TARGET0)
{
  outColor.w = 1;

  v1 = saturate(v1); // Be extra sure of not wrapping around as the sampler seems to be wrap
  float Y = texture0.Sample(sampler0_s, v1.xy).x;
  float Cr = texture1.Sample(sampler1_s, v1.xy).x;
  float Cb = texture2.Sample(sampler2_s, v1.xy).x;

  // Luma: use our own decode function, which matches the one that was here.
  // Videos were in limited range, and apparently BT.601 (encoded as, which means they need to be decoded the way to show properly in BT.709, as they original RGB value were before encoding)
  outColor.rgb = YUVtoRGB(Y, Cr, Cb, 3);

#if 0 // Test out of bounds values, to make sure the decoding was right!s
  outColor.rgb = abs(outColor.rgb - saturate(outColor.rgb)) * 1000;
#endif
}