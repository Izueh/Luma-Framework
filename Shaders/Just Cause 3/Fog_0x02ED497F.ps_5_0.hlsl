cbuffer cbConsts : register(b1)
{
  float4 Consts[2] : packoffset(c0);
}

SamplerState FogVolume_s : register(s0);
Texture2D<float4> FogVolume : register(t0);

#ifndef FORCE_VANILLA_FOG
#define FORCE_VANILLA_FOG 1
#endif // FORCE_VANILLA_FOG

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = FogVolume.Sample(FogVolume_s, v1.xy).xyzw; // TODO: skip upgrading textures on passes like these, given it serves very little purpose and worsens the performance
#if FORCE_VANILLA_FOG // Luma: force vanilla fog (clamped to 0-1 due to UNORM, but luma might upgrade the texture to FLOAT)
  r0.xyzw = saturate(r0.xyzw);
#else // Luma: clamping the alpha alone isn't enough!
  r0.z = saturate(r0.z);
#endif
  r1.xyz = Consts[1].xyz * r0.y;
  r0.x = r0.w * 4 + r0.x;
  o0.xyz = Consts[0].xyz * r0.x + r1.xyz;
  o0.w = r0.z;
}