cbuffer cbConstants : register(b1)
{
  float4 Constants : packoffset(c0);
}

SamplerState Sampler_s : register(s0);
Texture2D<float4> FrameTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  bool scaleDesat = (Constants.y != 0.0);
  float4 sceneColor = FrameTexture.Sample(Sampler_s, v1.xy).rgba; // linear to linear
  float luminance = dot(sceneColor.rgb, float3(0.212500006,0.715399981,0.0720999986));
  float scaledLuminance = saturate((luminance - 0.5) * Constants.z + 0.5);
  o0.xyz = lerp(sceneColor.rgb, scaleDesat ? scaledLuminance : luminance, Constants.x);
  o0.w = sceneColor.a;
}