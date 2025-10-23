SamplerState coronaTexture_s : register(s0);
Texture2D<float4> coronaTextureTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = coronaTextureTexture.Sample(coronaTexture_s, v1.xy).xyzw; // Note: if we wanted we could boost these to make them more HDR but overall it looks fine anyway
  o0.xyzw = v2.xyzw * r0.xyzw;
}