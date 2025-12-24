cbuffer _Globals : register(b0)
{
  float4 LuminanceValues : packoffset(c0);
}

SamplerState SceneLuminance_s : register(s0);
SamplerState LastAverageLuminance_s : register(s1);
Texture2D<float> SceneLuminanceTexture : register(t0);
Texture2D<float2> LastAverageLuminanceTexture : register(t1);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float2 o0 : SV_Target0)
{
  float2 lastAverageLuminances = LastAverageLuminanceTexture.Sample(LastAverageLuminance_s, float2(0,0)).xy; // Self feeding loop (ping ponged SRV/RTV between frames)
  float sceneLuminance = SceneLuminanceTexture.SampleLevel(SceneLuminance_s, float2(0,0), 10).x; // Smallest mip (texture is 1024x1024)
  o0.xy = lerp(lastAverageLuminances, sceneLuminance, LuminanceValues.xz); // Possibly the adjustment speed to account for frame rate
}