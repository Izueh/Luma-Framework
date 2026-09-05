Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

cbuffer cb0 : register(b0)
{
  float4 parameters;
}

void main(
  float4 position      : SV_POSITION,
  float4 vertexColor   : COLOR0,
  float2 centerUV      : TEXCOORD0,
  float4 neighborUV01  : TEXCOORD2,
  float4 neighborUV23  : TEXCOORD3,
  out float4 output    : SV_TARGET0)
{
  float3 filteredColor = SourceTexture.Sample(SourceSampler, centerUV).rgb;

#if 0 // Luma: disabled as I don't think this does anything good? Maybe it adds some blur to simulate AA? // TODO
  // Five-tap reconstruction filter. The center has twice the weight of each surrounding sample, giving a total weight of six.
  filteredColor *= 2.0;
  filteredColor += SourceTexture.Sample(SourceSampler, neighborUV01.xy).rgb;
  filteredColor += SourceTexture.Sample(SourceSampler, neighborUV01.zw).rgb;
  filteredColor += SourceTexture.Sample(SourceSampler, neighborUV23.xy).rgb;
  filteredColor += SourceTexture.Sample(SourceSampler, neighborUV23.zw).rgb;
  filteredColor *= 1.0 / 6.0;
#endif

  filteredColor *= vertexColor.rgb;

  // User brightness
  const float gammaExponent = parameters.x;
  // Luma: fix negative values passthrough
  output.rgb = pow(abs(filteredColor), gammaExponent) * sign(filteredColor);

  output.a = vertexColor.a;
}
