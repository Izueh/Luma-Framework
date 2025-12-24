#include "../Includes/Common.hlsl"

cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[48] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[211] : packoffset(c0);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
    float _69 = asfloat(cb4_m[192u].x);
    float _73 = asfloat(cb4_m[193u].x) - _69;
    float _79 = (abs(_73) > 0.0f) ? (1.0f / _73) : 9.999999933815812510711506376258e+36f;
    float2 _89 = float2(v5.x, v5.y);
    float _107 = asfloat(cb4_m[194u].x);
    float _111 = asfloat(cb4_m[195u].x) - _107;
    float _115 = (abs(_111) > 0.0f) ? (1.0f / _111) : 9.999999933815812510711506376258e+36f;
    float _123 = mad(asfloat(cb4_m[192u].y), asfloat(cb3_m[47u].x | (asuint(t1.Sample(s1, _89).x) & cb3_m[46u].x)), asfloat(cb4_m[192u].z));
    float _137 = ((asfloat(cb4_m[197u].x) - _123) >= 0.0f) ? max(clamp((_123 * _79) - (_69 * _79), 0.0f, 1.0f), clamp((_123 * _115) - (_107 * _115), 0.0f, 1.0f)) : 0.0f;
    float _141 = asfloat(cb4_m[196u].x) * _137;
    float _144 = asfloat(cb4_m[210u].y);
    float _148 = (abs(_144) > 0.0f) ? (1.0f / _144) : 9.999999933815812510711506376258e+36f;
    float _149 = _141 * _148;
    float _152 = asfloat(cb4_m[210u].x);
    float _156 = (abs(_152) > 0.0f) ? (1.0f / _152) : 9.999999933815812510711506376258e+36f;
    float _157 = _141 * _156;
    float4 _174 = t0.Sample(s0, float2(clamp(mad(_141, _156, v5.x), 0.0f, 1.0f), clamp(mad(_149, 0.5f, v5.y), 0.0f, 1.0f)));
    float4 _205 = t0.Sample(s0, float2(clamp(mad(_157, -0.5f, v5.x), 0.0f, 1.0f), clamp(mad(_141, _148, v5.y), 0.0f, 1.0f)));
    float4 _244 = t0.Sample(s0, float2(clamp(max(mad(_157, -1.0f, v5.x), 0.0f), 0.0f, 1.0f), clamp(max(mad(_149, -0.5f, v5.y), 0.0f), 0.0f, 1.0f)));
    float4 _271 = t0.Sample(s0, float2(clamp(max(mad(_157, 0.5f, v5.x), 0.0f), 0.0f, 1.0f), clamp(max(mad(_149, -1.0f, v5.y), 0.0f), 0.0f, 1.0f)));
    float4 _297 = t0.Sample(s0, _89);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _174.rgb = gamma_sRGB_to_linear(_174.rgb, GCT_MIRROR);
    _205.rgb = gamma_sRGB_to_linear(_205.rgb, GCT_MIRROR);
    _244.rgb = gamma_sRGB_to_linear(_244.rgb, GCT_MIRROR);
    _271.rgb = gamma_sRGB_to_linear(_271.rgb, GCT_MIRROR);
    _297.rgb = gamma_sRGB_to_linear(_297.rgb, GCT_MIRROR);
#endif
    float _316 = asfloat((cb3_m[44u].x & asuint(_297.x)) | cb3_m[45u].x);
    float _317 = asfloat((cb3_m[44u].y & asuint(_297.y)) | cb3_m[45u].y);
    float _318 = asfloat((asuint(_297.z) & cb3_m[44u].z) | cb3_m[45u].z);
    o0.x = mad(mad((((asfloat((cb3_m[44u].x & asuint(_174.x)) | cb3_m[45u].x) + asfloat((cb3_m[44u].x & asuint(_205.x)) | cb3_m[45u].x)) + asfloat((cb3_m[44u].x & asuint(_244.x)) | cb3_m[45u].x)) + asfloat((cb3_m[44u].x & asuint(_271.x)) | cb3_m[45u].x)) + _316, 0.20000000298023223876953125f, -_316), _137, _316);
    o0.y = mad(mad((asfloat((cb3_m[44u].y & asuint(_271.y)) | cb3_m[45u].y) + (asfloat((cb3_m[44u].y & asuint(_244.y)) | cb3_m[45u].y) + (asfloat(cb3_m[45u].y | (cb3_m[44u].y & asuint(_174.y))) + asfloat((cb3_m[44u].y & asuint(_205.y)) | cb3_m[45u].y)))) + _317, 0.20000000298023223876953125f, -_317), _137, _317);
    o0.z = mad(mad((asfloat((asuint(_271.z) & cb3_m[44u].z) | cb3_m[45u].z) + (asfloat((asuint(_244.z) & cb3_m[44u].z) | cb3_m[45u].z) + (asfloat(cb3_m[45u].z | (asuint(_174.z) & cb3_m[44u].z)) + asfloat((asuint(_205.z) & cb3_m[44u].z) | cb3_m[45u].z)))) + _318, 0.20000000298023223876953125f, -_318), _137, _318);
    o0.w = ((asfloat((cb3_m[44u].w & asuint(_271.w)) | cb3_m[45u].w) + (asfloat(cb3_m[45u].w | (cb3_m[44u].w & asuint(_244.w))) + (asfloat(cb3_m[45u].w | (cb3_m[44u].w & asuint(_174.w))) + asfloat(cb3_m[45u].w | (cb3_m[44u].w & asuint(_205.w)))))) + asfloat((cb3_m[44u].w & asuint(_297.w)) | cb3_m[45u].w)) * 0.20000000298023223876953125f;
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR);
#endif
}