// Luma: ps_5_0
// Luma: ps_5_0 Luma_Scale_Table_PS TABLE=1
#if TABLE && defined(LUMA_TABLE_TEXTURE_INDEX)
Texture2D<float4> sourceTexture : register(LUMA_TABLE_TEXTURE_INDEX);
#else
Texture2D<float4> sourceTexture : register(t0);
#endif
#if TABLE && defined(LUMA_LINEAR_CLAMP_SAMPLER_INDEX)
SamplerState sourceSampler : register(LUMA_LINEAR_CLAMP_SAMPLER_INDEX);
#elif !TABLE
SamplerState sourceSampler : register(s0);
#endif

// Bilinear scaling copy: samples the source texture with normalized UVs, so it can
// copy between same-aspect different-resolution resources (e.g. render-res <-> output-res mirrors).
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
#if TABLE && !defined(LUMA_LINEAR_CLAMP_SAMPLER_INDEX)
	uint width = 1, height = 1;
	sourceTexture.GetDimensions(width, height);
	float2 coord = uv * float2(width, height) - 0.5;
	int2 p0 = clamp(int2(floor(coord)), int2(0, 0), int2(width - 1, height - 1));
	int2 p1 = min(p0 + int2(1, 1), int2(width - 1, height - 1));
	float2 f = saturate(frac(coord));
	float4 c00 = sourceTexture.Load(int3(p0.x, p0.y, 0));
	float4 c10 = sourceTexture.Load(int3(p1.x, p0.y, 0));
	float4 c01 = sourceTexture.Load(int3(p0.x, p1.y, 0));
	float4 c11 = sourceTexture.Load(int3(p1.x, p1.y, 0));
	return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
#else
	return sourceTexture.SampleLevel(sourceSampler, uv, 0);
#endif
}
