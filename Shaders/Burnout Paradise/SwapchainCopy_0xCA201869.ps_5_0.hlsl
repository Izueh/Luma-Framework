cbuffer _Globals : register(b0)
{
  float4 gv4GammaValues : packoffset(c0);
}

SamplerState Scene_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = SceneTexture.Sample(Scene_s, v1.xy).xyzw;
  // Applies the user gamma brightness value, defaults at 1
  o0.xyz = pow(abs(o0.xyz), gv4GammaValues.x) * sign(o0.xyz); // Luma: fixed support for negative values
}