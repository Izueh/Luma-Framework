#ifndef SRC_SAMPLERS_HLSL
#define SRC_SAMPLERS_HLSL

// D3D12 only, when Game::Options::bindings.static_samplers is enabled
#ifdef LUMA_POINT_CLAMP_SAMPLER_INDEX
#define LUMA_HAS_STATIC_SAMPLERS 1
SamplerState LumaPointClampSampler : register(LUMA_POINT_CLAMP_SAMPLER_INDEX);
SamplerState LumaLinearClampSampler : register(LUMA_LINEAR_CLAMP_SAMPLER_INDEX);
SamplerState LumaLinearWrapSampler : register(LUMA_LINEAR_WRAP_SAMPLER_INDEX);
SamplerState LumaAnisotropicWrapSampler : register(LUMA_ANISOTROPIC_WRAP_SAMPLER_INDEX); // 16x
SamplerState LumaPointWrapSampler : register(LUMA_POINT_WRAP_SAMPLER_INDEX);
#else
#define LUMA_HAS_STATIC_SAMPLERS 0
#endif

#endif // SRC_SAMPLERS_HLSL
