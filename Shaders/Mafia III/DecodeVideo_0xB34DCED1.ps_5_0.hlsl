#include "Includes/Common.hlsl"

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// NV12/YUV420 sampling
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	float Y = t0.Sample(s0_s, v1.xy).x;
	float Cr = t1.Sample(s1_s, v1.zw).x;
	float Cb = t2.Sample(s2_s, v1.zw).x;
	float Alpha = t3.Sample(s3_s, v1.xy).x;
  
#if FIX_VIDEOS_COLOR_SPACE
  outColor.rgb = YUVtoRGB(Y, Cr, Cb, 0);
#else // Incorrect red levels, skin looks red
  outColor.rgb = YUVtoRGB(Y, Cr, Cb, 2);
#endif
#if 0 // Test out of bounds values, to make sure the decoding was right!s
  outColor.rgb = abs(outColor.rgb - saturate(outColor.rgb)) * 1000;
#endif
  
  // Alpha channel
  outColor.a = Alpha;
}