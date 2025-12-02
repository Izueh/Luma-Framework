#include "Includes/Common.hlsl"

cbuffer cbConsts : register(b1)
{
  float4 Consts : packoffset(c0);
}

SamplerState RenderTarget_s : register(s0);
Texture2D<float4> RenderTarget : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyz = RenderTarget.Sample(RenderTarget_s, v1.xy).xyz;
#if 1 // Luma
  r0.xyz = max(r0.xyz, 0.0); // Fix Nans and remove negative values (they'd be trash)
#endif
  r0.xyz = Consts.y * r0.xyz; // Auto exposure (same one from the tonemapper, which means it will be influenced by HDR!)
  r0.w = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
  r0.w = -Consts.z + r0.w;
  r0.xyz = r0.xyz * r0.w;
  r0.xyz = max(r0.xyz, 0.0);
  o0.xyz = pow(r0.xyz, Consts.x);
#if 1 // Luma: optionally remove "unnecessary" limit, it probably doesn't hurt anyway
  o0.xyz = min(o0.xyz, 4094.0);
#endif
  o0.w = 1;
}