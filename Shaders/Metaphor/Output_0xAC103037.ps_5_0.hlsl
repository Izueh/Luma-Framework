#include "Includes/Common.hlsl"
#include "Includes/Sample.hlsl"
#include "Includes/Tonemapper.hlsl"

SamplerState linearSampler_s : register(s0);
SamplerState nearestSampler_s : register(s1);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> bloomTexture : register(t1);
Texture2D<float4> effectTexture : register(t2);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_Target0)
{
	float4 r0,r1,r2;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.xyzw = sample_bicubic(bloomTexture, linearSampler_s, v1.xy);
	r1.xyzw = effectTexture.Sample(nearestSampler_s, v1.xy).xyzw;
	r2.xyzw = colorTexture.Sample(nearestSampler_s, v1.xy).xyzw;
	r1.xyz = r2.xyz * r1.www + r1.xyz;
	o0.w = r2.w;
	o0.xyz = r0.xyz * r0.www + r1.xyz;

	o0.rgb = ApplyUserTonemap(o0.rgb);
	return;
}