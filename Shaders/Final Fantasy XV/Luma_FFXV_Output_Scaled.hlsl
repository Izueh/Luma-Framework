// Copy source to rtv linearly scared.
#include "Includes/Common.hlsl"

SamplerState linear_sampler : register(s0);
Texture2D input_texture : register(t0);

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    float2 uv = pos.xy * LumaSettings.GameSettings.RenderResolution.zw;
    return input_texture.Sample(linear_sampler, uv);
}