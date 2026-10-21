Texture2D<float4> rainLayer : register(t0);
SamplerState rainSampler : register(s0);

cbuffer RainComposite : register(b0)
{
   float2 outputTexelSize;
   float2 jitterUV;
}

float4 main(float4 position : SV_POSITION) : SV_TARGET0
{
   // Reconstruct premultiplied RGB and coverage together. A stable output texel
   // addresses the original jittered rain grid using the captured TAA jitter.
   return rainLayer.SampleLevel(rainSampler, position.xy * outputTexelSize + jitterUV, 0);
}
