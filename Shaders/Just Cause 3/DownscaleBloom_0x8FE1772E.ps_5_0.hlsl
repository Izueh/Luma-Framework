#include "Includes/Common.hlsl"

cbuffer cbConsts : register(b1)
{
  float4 Consts : packoffset(c0);
}

#ifndef FORCE_VANILLA_AUTO_EXPOSURE_TYPE
#define FORCE_VANILLA_AUTO_EXPOSURE_TYPE 0
#endif // FORCE_VANILLA_AUTO_EXPOSURE_TYPE

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
  r0.xyz = Consts.y * r0.xyz; // "Late" Auto exposure coeff (same one from the tonemapper, which means it might be influenced by HDR!). This also means bloom is affected by auto exposure twice, which is very weird, though maybe one is a division and one is a multiplication.
#if FORCE_VANILLA_AUTO_EXPOSURE_TYPE >= 1
  r0.w = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343)); // Average (makes little sense, don't know why not luminance)
#else
  r0.w = GetLuminance(r0.xyz);
#endif
  r0.w = -Consts.z + r0.w; // "Early" Auto Exposure coeff. Basically any color average above the exposure level becomes bloom.
  r0.xyz = r0.xyz * r0.w;
  r0.xyz = max(r0.xyz, 0.0);
  o0.xyz = pow(r0.xyz, Consts.x);
#if 1 // Luma: optionally remove "unnecessary" limit, it probably doesn't hurt anyway
  o0.xyz = min(o0.xyz, 4094.0);
#endif
  o0.w = 1;
}