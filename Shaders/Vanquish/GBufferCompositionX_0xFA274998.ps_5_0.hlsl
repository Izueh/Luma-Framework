#include "../Includes/Common.hlsl"

cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[56] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[211] : packoffset(c0);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s5 : register(s5);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t5 : register(t5);

float dp3_f32(float3 a, float3 b)
{
    precise float _76 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _76));
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
    float _94 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _107 = asfloat(cb4_m[210u].x);
    float _114 = asfloat(cb4_m[210u].y);
    float _119 = mad((abs(_107) > 0.0f) ? (1.0f / _107) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _94, 0.5f, 0.5f));
    float _120 = mad((abs(_114) > 0.0f) ? (1.0f / _114) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _94, -0.5f, 0.5f));
    float2 _126 = float2(_119, _120);
    float _149 = mad(asfloat((asuint(t5.Sample(s5, _126).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _154 = asfloat(cb4_m[12u].x);
    float _161 = asfloat(cb4_m[13u].y);
    float _175 = mad(-(mad(_119, 2.0f, -1.0f) * _149), (abs(_154) > 0.0f) ? (1.0f / _154) : 9.999999933815812510711506376258e+36f, asfloat(cb4_m[192u].x));
    float _177 = mad(-((abs(_161) > 0.0f) ? (1.0f / _161) : 9.999999933815812510711506376258e+36f), mad(_120, -2.0f, 1.0f) * _149, asfloat(cb4_m[192u].y));
    float _178 = _149 + asfloat(cb4_m[192u].z);
    float3 _179 = float3(_175, _177, _178);
    float _182 = rsqrt(abs(dp3_f32(_179, _179)));
    float _185 = (asuint(_182) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _182;
    float4 _196 = t1.Sample(s1, _126);
    float4 _228 = t0.Sample(s0, _126);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _228.rgb = gamma_sRGB_to_linear(_228.rgb, GCT_MIRROR);
#endif
    float _257 = asfloat(cb3_m[47u].w | (cb3_m[46u].w & asuint(_196.w)));
    float _271 = clamp((((0.49500000476837158203125f - _257) >= 0.0f) ? 0.0f : 1.0f) - (((_257 - 0.49500000476837158203125f) >= 0.0f) ? 0.0f : 1.0f), 0.0f, 1.0f);
    float _275 = clamp(dp3_f32(float3(_175 * _185, _177 * _185, _178 * _185), float3(mad(asfloat(cb3_m[47u].x | (asuint(_196.x) & cb3_m[46u].x)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (asuint(_196.y) & cb3_m[46u].y)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (cb3_m[46u].z & asuint(_196.z))), 2.0f, -1.0f))), 0.0f, 1.0f);
    float _293 = asfloat(cb4_m[193u].w);
    float _302 = clamp((asfloat(cb4_m[192u].w) - ((abs(_185) > 0.0f) ? (1.0f / _185) : 9.999999933815812510711506376258e+36f)) * ((abs(_293) > 0.0f) ? (1.0f / _293) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _316 = clamp(clamp(_275 - 0.5f, 0.0f, 1.0f) + asfloat(cb3_m[45u].w | (asuint(_228.w) & cb3_m[44u].w)), 0.0f, 1.0f);
    float _321 = clamp(mad(_302, 0.89999997615814208984375f, 0.100000001490116119384765625f), 0.0f, 1.0f);
    o0.w = _302;
    o0.x = (((((_271 * asfloat(cb4_m[193u].x)) * _275) * _302) * asfloat((cb3_m[44u].x & asuint(_228.x)) | cb3_m[45u].x)) * _316) * _321;
    o0.y = ((((_275 * (_271 * asfloat(cb4_m[193u].y))) * _302) * asfloat(cb3_m[45u].y | (asuint(_228.y) & cb3_m[44u].y))) * _316) * _321;
    o0.z = ((((_275 * (_271 * asfloat(cb4_m[193u].z))) * _302) * asfloat((asuint(_228.z) & cb3_m[44u].z) | cb3_m[45u].z)) * _316) * _321;
}