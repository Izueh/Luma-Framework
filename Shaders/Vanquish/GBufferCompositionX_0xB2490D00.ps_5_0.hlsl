#include "../Includes/Common.hlsl"

cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[58] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[211] : packoffset(c0);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s4 : register(s4);
SamplerState s5 : register(s5);
SamplerState s6 : register(s6);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t6 : register(t6);

float dp3_f32(float3 a, float3 b)
{
    precise float _83 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _83));
}

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
    float _98 = asfloat(cb4_m[210u].x);
    float _106 = asfloat(cb4_m[210u].y);
    float2 _121 = float2(mad((abs(_98) > 0.0f) ? (1.0f / _98) : 9.999999933815812510711506376258e+36f, 0.5f, v5.x), mad((abs(_106) > 0.0f) ? (1.0f / _106) : 9.999999933815812510711506376258e+36f, 0.5f, v5.y));
    float _144 = mad(asfloat(cb3_m[55u].x | (asuint(t5.Sample(s5, _121).x) & cb3_m[54u].x)), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _151 = _144 * v6.x;
    float _152 = _144 * v6.y;
    float _153 = _144 * v6.z;
    float3 _157 = float3(-_151, -_152, -_153);
    float _160 = rsqrt(abs(dp3_f32(_157, _157)));
    float _163 = (asuint(_160) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _160;
    float _169 = asfloat(cb4_m[193u].x);
    float _170 = asfloat(cb4_m[193u].y);
    float _171 = asfloat(cb4_m[193u].z);
    float _173 = mad(-_151, _163, _169);
    float _175 = mad(-_152, _163, _170);
    float _177 = mad(-_153, _163, _171);
    float3 _178 = float3(_173, _175, _177);
    float _180 = rsqrt(dp3_f32(_178, _178));
    float _183 = (asuint(_180) != 2139095040u) ? _180 : 0.0f;
    float4 _190 = t1.Sample(s1, _121);
    float3 _226 = float3(mad(asfloat((asuint(_190.x) & cb3_m[46u].x) | cb3_m[47u].x), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (asuint(_190.y) & cb3_m[46u].y)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_190.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float _230 = clamp(dp3_f32(float3(_169, _170, _171), _226), 0.0f, 1.0f);
    float4 _235 = t2.Sample(s2, _121);
    float _252 = asfloat(cb3_m[49u].w | (asuint(_235.w) & cb3_m[48u].w));
    float _254 = 1.0f - _252;
    float _255 = asfloat(cb3_m[47u].w | (asuint(_190.w) & cb3_m[46u].w));
    float _263 = asfloat(cb3_m[49u].z | (asuint(_235.z) & cb3_m[48u].z)) * 5.0f;
    float _265 = clamp((((0.49500000476837158203125f - _255) >= 0.0f) ? 0.0f : 1.0f) - (((_255 - 0.49500000476837158203125f) >= 0.0f) ? 0.0f : 1.0f), 0.0f, 1.0f);
    float _271 = mad(_265, -0.5f, _255);
    float4 _280 = t4.Sample(s4, float2(clamp((max(dp3_f32(float3(_173 * _183, _183 * _175, _183 * _177), _226), 0.0f) - _252) * clamp((abs(_254) > 0.0f) ? (1.0f / _254) : 9.999999933815812510711506376258e+36f, 0.0f, 1.0f), 0.0f, 1.0f), clamp(_271 + _271, 0.0f, 1.0f)));
    float4 _312 = t0.Sample(s0, _121);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _312.rgb = gamma_sRGB_to_linear(_312.rgb, GCT_MIRROR);
#endif
    float _379 = clamp(clamp(_230 - 0.5f, 0.0f, 1.0f) + asfloat(cb3_m[45u].w | (asuint(_312.w) & cb3_m[44u].w)), 0.0f, 1.0f);
    float _386 = asfloat(cb3_m[57u].w | (cb3_m[56u].w & asuint(t6.Sample(s6, _121).w)));
    o0.x = ((_265 * ((((_263 * asfloat((asuint(_280.x) & cb3_m[52u].x) | cb3_m[53u].x)) + (_230 * asfloat((asuint(_312.x) & cb3_m[44u].x) | cb3_m[45u].x))) * asfloat(cb4_m[194u].x)) * _379)) * _386) * (asfloat(cb4_m[195u].x) * asfloat(cb4_m[196u].x));
    o0.y = (asfloat(cb4_m[195u].y) * asfloat(cb4_m[196u].y)) * ((_265 * ((((_230 * asfloat(cb3_m[45u].y | (asuint(_312.y) & cb3_m[44u].y))) + (_263 * asfloat(cb3_m[53u].y | (asuint(_280.y) & cb3_m[52u].y)))) * asfloat(cb4_m[194u].y)) * _379)) * _386);
    o0.z = (asfloat(cb4_m[195u].z) * asfloat(cb4_m[196u].z)) * ((_265 * ((((_230 * asfloat(cb3_m[45u].z | (asuint(_312.z) & cb3_m[44u].z))) + (_263 * asfloat(cb3_m[53u].z | (asuint(_280.z) & cb3_m[52u].z)))) * asfloat(cb4_m[194u].z)) * _379)) * _386);
    o0.w = 1.0f;
}
