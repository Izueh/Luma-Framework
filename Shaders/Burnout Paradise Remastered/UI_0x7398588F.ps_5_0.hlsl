#include "Includes/Common.hlsl"

SamplerState DiffuseSampler_s : register(s0);
Texture2D<float4> DiffuseSamplerTexture : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = DiffuseSamplerTexture.Sample(DiffuseSampler_s, v3.xy).xyzw;
  
	float2 size;
	DiffuseSamplerTexture.GetDimensions(size.x, size.y);
#if REMOVE_BLACK_BARS
  // Remove badly placed menu black bars
  if (size.x == 4.0 && size.y == 4.0 && all(r0.xyz == 1.0) && all(v2.xyz == 0.0) && all(v1.xyzw == 0.0))
  {
    r0.w = 0;
  }
#endif

  o0.xyzw = r0.xyzw * v2.xyzw + v1.xyzw;
  
  // Luma: UNORM RT emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.w = saturate(o0.w);
}