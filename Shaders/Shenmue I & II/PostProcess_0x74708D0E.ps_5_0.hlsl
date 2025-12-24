SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> diffuseTexture0 : register(t0); // Scene
Texture2D<float4> diffuseTexture1 : register(t1); // 2x2 4 channel texture
Texture2D<float4> diffuseTexture2 : register(t2); // 2x2 4 channel texture
Texture2D<float4> diffuseTexture3 : register(t3); // 2x2 4 channel texture

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float2 v2 : TEXCOORD2,
  float2 w2 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyz = diffuseTexture0.Sample(DiffuseSampler_s, v1.xy).xyz;
  r1.xyzw = diffuseTexture1.Sample(DiffuseSampler_s, w1.xy).xyzw;
  r0.w = 1;
  r0.xyzw = r1.xyzw * r0.xyzw;
  r1.xyzw = diffuseTexture2.Sample(DiffuseSampler_s, v2.xy).xyzw;
  r0.xyzw = r1.xyzw * r0.xyzw;
  r1.xyzw = diffuseTexture3.Sample(DiffuseSampler_s, w2.xy).xyzw;
  o0.xyzw = r1.xyzw * r0.xyzw;
}