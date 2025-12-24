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
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s5 : register(s5);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t5 : register(t5);

float dp3_f32(float3 a, float3 b)
{
    precise float _86 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _86));
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
    float _104 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _117 = asfloat(cb4_m[210u].x);
    float _124 = asfloat(cb4_m[210u].y);
    float _129 = mad((abs(_117) > 0.0f) ? (1.0f / _117) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _104, 0.5f, 0.5f));
    float _130 = mad((abs(_124) > 0.0f) ? (1.0f / _124) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _104, -0.5f, 0.5f));
    float _133 = asfloat(cb4_m[12u].x);
    float _137 = (abs(_133) > 0.0f) ? (1.0f / _133) : 9.999999933815812510711506376258e+36f;
    float2 _143 = float2(_129, _130);
    float _166 = mad(asfloat((asuint(t5.Sample(s5, _143).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _169 = asfloat(cb4_m[13u].y);
    float _173 = (abs(_169) > 0.0f) ? (1.0f / _169) : 9.999999933815812510711506376258e+36f;
    float _174 = mad(_129, 2.0f, -1.0f) * _166;
    float _175 = mad(_130, -2.0f, 1.0f) * _166;
    float _176 = _174 * _137;
    float _177 = _173 * _175;
    float _189 = mad(-_174, _137, asfloat(cb4_m[192u].x));
    float _191 = mad(-_173, _175, asfloat(cb4_m[192u].y));
    float _192 = _166 + asfloat(cb4_m[192u].z);
    float3 _193 = float3(_189, _191, _192);
    float3 _195 = float3(-_176, -_177, _166);
    float _198 = rsqrt(abs(dp3_f32(_193, _193)));
    float _201 = (asuint(_198) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _198;
    float _203 = rsqrt(abs(dp3_f32(_195, _195)));
    float _206 = (asuint(_203) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _203;
    float _207 = _189 * _201;
    float _208 = _201 * _191;
    float _209 = _192 * _201;
    float _213 = _207 - (_176 * _206);
    float _214 = _208 - (_206 * _177);
    float _215 = (_166 * _206) + _209;
    float3 _216 = float3(_213, _214, _215);
    float _218 = rsqrt(dp3_f32(_216, _216));
    float _221 = (asuint(_218) != 2139095040u) ? _218 : 0.0f;
    float4 _228 = t1.Sample(s1, _143);
    float3 _268 = float3(mad(asfloat(cb3_m[47u].x | (cb3_m[46u].x & asuint(_228.x))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (cb3_m[46u].y & asuint(_228.y))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_228.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float _272 = clamp(dp3_f32(float3(_207, _208, _209), _268), 0.0f, 1.0f);
    float4 _277 = t2.Sample(s2, _143);
    float4 _297 = t0.Sample(s0, _143);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _297.rgb = gamma_sRGB_to_linear(_297.rgb, GCT_MIRROR);
#endif
    float _326 = asfloat((asuint(_277.w) & cb3_m[48u].w) | cb3_m[49u].w);
    float _328 = 1.0f - _326;
    float _330 = asfloat((cb3_m[48u].z & asuint(_277.z)) | cb3_m[49u].z) * 5.0f;
    float _331 = asfloat(cb3_m[47u].w | (asuint(_228.w) & cb3_m[46u].w));
    float _344 = clamp((((0.49500000476837158203125f - _331) >= 0.0f) ? (-0.0f) : 1.0f) - (((_331 - 0.49500000476837158203125f) >= 0.0f) ? (-0.0f) : 1.0f), 0.0f, 1.0f);
    float _347 = mad(_344, -0.5f, _331);
    float _356 = _344 * asfloat(cb4_m[193u].x);
    float _357 = _344 * asfloat(cb4_m[193u].y);
    float _358 = _344 * asfloat(cb4_m[193u].z);
    float4 _365 = t3.Sample(s3, float2(clamp((max(dp3_f32(float3(_213 * _221, _221 * _214, _221 * _215), _268), -0.0f) - _326) * clamp((abs(_328) > 0.0f) ? (1.0f / _328) : 9.999999933815812510711506376258e+36f, 0.0f, 1.0f), 0.0f, 1.0f), clamp(_347 + _347, 0.0f, 1.0f)));
    float _405 = asfloat(cb4_m[193u].w);
    float _414 = clamp((asfloat(cb4_m[192u].w) - ((abs(_201) > 0.0f) ? (1.0f / _201) : 9.999999933815812510711506376258e+36f)) * ((abs(_405) > 0.0f) ? (1.0f / _405) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _432 = clamp(clamp(_272 - 0.5f, 0.0f, 1.0f) + asfloat((asuint(_297.w) & cb3_m[44u].w) | cb3_m[45u].w), 0.0f, 1.0f);
    float _437 = clamp(mad(_414, 0.89999997615814208984375f, 0.100000001490116119384765625f), 0.0f, 1.0f);
    o0.w = _414;
    o0.x = ((((_330 * (_356 * asfloat(cb3_m[51u].x | (asuint(_365.x) & cb3_m[50u].x)))) * _414) + (((_272 * _356) * _414) * asfloat((asuint(_297.x) & cb3_m[44u].x) | cb3_m[45u].x))) * _432) * _437;
    o0.y = (((((_272 * _357) * _414) * asfloat((asuint(_297.y) & cb3_m[44u].y) | cb3_m[45u].y)) + ((_330 * (asfloat(cb3_m[51u].y | (asuint(_365.y) & cb3_m[50u].y)) * _357)) * _414)) * _432) * _437;
    o0.z = (((((_272 * _358) * _414) * asfloat((asuint(_297.z) & cb3_m[44u].z) | cb3_m[45u].z)) + ((_330 * (asfloat(cb3_m[51u].z | (asuint(_365.z) & cb3_m[50u].z)) * _358)) * _414)) * _432) * _437;
}