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
    precise float _78 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _78));
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
    float _96 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _109 = asfloat(cb4_m[210u].x);
    float _116 = asfloat(cb4_m[210u].y);
    float _121 = mad((abs(_109) > 0.0f) ? (1.0f / _109) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _96, 0.5f, 0.5f));
    float _122 = mad((abs(_116) > 0.0f) ? (1.0f / _116) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _96, -0.5f, 0.5f));
    float2 _128 = float2(_121, _122);
    float _151 = mad(asfloat((asuint(t5.Sample(s5, _128).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _152 = mad(_121, 2.0f, -1.0f) * _151;
    float _153 = mad(_122, -2.0f, 1.0f) * _151;
    float _156 = asfloat(cb4_m[12u].x);
    float _160 = (abs(_156) > 0.0f) ? (1.0f / _156) : 9.999999933815812510711506376258e+36f;
    float _163 = asfloat(cb4_m[13u].y);
    float _167 = (abs(_163) > 0.0f) ? (1.0f / _163) : 9.999999933815812510711506376258e+36f;
    float _173 = asfloat(cb4_m[192u].x);
    float _174 = asfloat(cb4_m[192u].y);
    float _175 = asfloat(cb4_m[192u].z);
    float _184 = asfloat(cb4_m[193u].x) - _173;
    float _185 = asfloat(cb4_m[193u].y) - _174;
    float _186 = asfloat(cb4_m[193u].z) - _175;
    float3 _193 = float3(_184, _185, _186);
    float _194 = dp3_f32(_193, _193);
    float _202 = clamp(dp3_f32(float3(mad(_152, _160, -_173), mad(_167, _153, -_174), -(_151 + _175)), _193) * ((abs(_194) > 0.0f) ? (1.0f / _194) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _207 = mad(-_152, _160, mad(_184, _202, _173));
    float _209 = mad(-_167, _153, mad(_202, _185, _174));
    float _210 = _151 + mad(_202, _186, _175);
    float3 _211 = float3(_207, _209, _210);
    float _214 = rsqrt(abs(dp3_f32(_211, _211)));
    float _217 = (asuint(_214) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _214;
    float4 _228 = t1.Sample(s1, _128);
    float4 _260 = t0.Sample(s0, _128);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _260.rgb = gamma_sRGB_to_linear(_260.rgb, GCT_MIRROR);
#endif
    float _289 = asfloat(cb3_m[47u].w | (asuint(_228.w) & cb3_m[46u].w));
    float _303 = clamp((((0.49500000476837158203125f - _289) >= 0.0f) ? (-0.0f) : 1.0f) - (((_289 - 0.49500000476837158203125f) >= 0.0f) ? (-0.0f) : 1.0f), 0.0f, 1.0f);
    float _307 = clamp(dp3_f32(float3(_207 * _217, _209 * _217, _210 * _217), float3(mad(asfloat(cb3_m[47u].x | (asuint(_228.x) & cb3_m[46u].x)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (cb3_m[46u].y & asuint(_228.y))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_228.z) & cb3_m[46u].z)), 2.0f, -1.0f))), 0.0f, 1.0f);
    float _325 = asfloat(cb4_m[194u].w);
    float _334 = clamp((asfloat(cb4_m[192u].w) - ((abs(_217) > 0.0f) ? (1.0f / _217) : 9.999999933815812510711506376258e+36f)) * ((abs(_325) > 0.0f) ? (1.0f / _325) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _348 = clamp(clamp(_307 - 0.5f, 0.0f, 1.0f) + asfloat((asuint(_260.w) & cb3_m[44u].w) | cb3_m[45u].w), 0.0f, 1.0f);
    float _353 = clamp(mad(_334, 0.89999997615814208984375f, 0.100000001490116119384765625f), 0.0f, 1.0f);
    o0.w = _334;
    o0.x = (((((_303 * asfloat(cb4_m[194u].x)) * _307) * _334) * asfloat((cb3_m[44u].x & asuint(_260.x)) | cb3_m[45u].x)) * _348) * _353;
    o0.y = (((((_303 * asfloat(cb4_m[194u].y)) * _307) * _334) * asfloat(cb3_m[45u].y | (asuint(_260.y) & cb3_m[44u].y))) * _348) * _353;
    o0.z = (((((_303 * asfloat(cb4_m[194u].z)) * _307) * _334) * asfloat(cb3_m[45u].z | (cb3_m[44u].z & asuint(_260.z)))) * _348) * _353;
}