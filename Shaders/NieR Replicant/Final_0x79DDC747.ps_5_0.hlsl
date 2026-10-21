#include "Includes/Common.hlsl"

SamplerState tex_Color_s_s : register(s0);
Texture2D<float4> tex_Color : register(t0);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	float4 r0,r1,r2;

	r0.xyz = tex_Color.Sample(tex_Color_s_s, v1.xy).xyz;
	o0.xyz = linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR);
	o0.w = 1;
}