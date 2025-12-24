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
    precise float _87 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _87));
}

// TODO: there's probably more of these that are still missing
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
    float _105 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _118 = asfloat(cb4_m[210u].x);
    float _125 = asfloat(cb4_m[210u].y);
    float _130 = mad((abs(_118) > 0.0f) ? (1.0f / _118) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _105, 0.5f, 0.5f));
    float _131 = mad((abs(_125) > 0.0f) ? (1.0f / _125) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _105, -0.5f, 0.5f));
    float _134 = asfloat(cb4_m[12u].x);
    float _138 = (abs(_134) > 0.0f) ? (1.0f / _134) : 9.999999933815812510711506376258e+36f;
    float2 _144 = float2(_130, _131);
    float _167 = mad(asfloat((asuint(t5.Sample(s5, _144).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _170 = asfloat(cb4_m[13u].y);
    float _174 = (abs(_170) > 0.0f) ? (1.0f / _170) : 9.999999933815812510711506376258e+36f;
    float _175 = mad(_130, 2.0f, -1.0f) * _167;
    float _176 = mad(_131, -2.0f, 1.0f) * _167;
    float _177 = _175 * _138;
    float _178 = _174 * _176;
    float _184 = asfloat(cb4_m[192u].x);
    float _185 = asfloat(cb4_m[192u].y);
    float _186 = asfloat(cb4_m[192u].z);
    float _195 = asfloat(cb4_m[193u].x) - _184;
    float _196 = asfloat(cb4_m[193u].y) - _185;
    float _197 = asfloat(cb4_m[193u].z) - _186;
    float3 _204 = float3(_195, _196, _197);
    float _205 = dp3_f32(_204, _204);
    float _213 = clamp(dp3_f32(float3(mad(_175, _138, -_184), mad(_174, _176, -_185), -(_167 + _186)), _204) * ((abs(_205) > 0.0f) ? (1.0f / _205) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _220 = mad(-_175, _138, mad(_195, _213, _184));
    float _222 = mad(-_174, _176, mad(_213, _196, _185));
    float _223 = _167 + mad(_213, _197, _186);
    float3 _224 = float3(_220, _222, _223);
    float _227 = rsqrt(abs(dp3_f32(_224, _224)));
    float _230 = (asuint(_227) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _227;
    float3 _231 = float3(-_177, -_178, _167);
    float _233 = _220 * _230;
    float _234 = _222 * _230;
    float _235 = _223 * _230;
    float _237 = rsqrt(abs(dp3_f32(_231, _231)));
    float _240 = (asuint(_237) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _237;
    float _244 = _233 - (_177 * _240);
    float _245 = _234 - (_240 * _178);
    float _246 = (_167 * _240) + _235;
    float3 _247 = float3(_244, _245, _246);
    float _249 = rsqrt(dp3_f32(_247, _247));
    float _252 = (asuint(_249) != 2139095040u) ? _249 : 0.0f;
    float4 _259 = t1.Sample(s1, _144);
    float3 _299 = float3(mad(asfloat(cb3_m[47u].x | (cb3_m[46u].x & asuint(_259.x))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (cb3_m[46u].y & asuint(_259.y))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_259.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float _303 = clamp(dp3_f32(float3(_233, _234, _235), _299), 0.0f, 1.0f);
    float4 _308 = t2.Sample(s2, _144);
    float4 _328 = t0.Sample(s0, _144);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _328.rgb = gamma_sRGB_to_linear(_328.rgb, GCT_MIRROR);
#endif
    float _357 = asfloat((asuint(_308.w) & cb3_m[48u].w) | cb3_m[49u].w);
    float _359 = 1.0f - _357;
    float _361 = asfloat((cb3_m[48u].z & asuint(_308.z)) | cb3_m[49u].z) * 5.0f;
    float _362 = asfloat(cb3_m[47u].w | (cb3_m[46u].w & asuint(_259.w)));
    float _375 = clamp((((0.49500000476837158203125f - _362) >= 0.0f) ? (-0.0f) : 1.0f) - (((_362 - 0.49500000476837158203125f) >= 0.0f) ? (-0.0f) : 1.0f), 0.0f, 1.0f);
    float _378 = mad(_375, -0.5f, _362);
    float _387 = _375 * asfloat(cb4_m[194u].x);
    float _388 = _375 * asfloat(cb4_m[194u].y);
    float _389 = _375 * asfloat(cb4_m[194u].z);
    float4 _396 = t3.Sample(s3, float2(clamp((max(dp3_f32(float3(_244 * _252, _252 * _245, _252 * _246), _299), -0.0f) - _357) * clamp((abs(_359) > 0.0f) ? (1.0f / _359) : 9.999999933815812510711506376258e+36f, 0.0f, 1.0f), 0.0f, 1.0f), clamp(_378 + _378, 0.0f, 1.0f)));
    float _436 = asfloat(cb4_m[194u].w);
    float _445 = clamp((asfloat(cb4_m[192u].w) - ((abs(_230) > 0.0f) ? (1.0f / _230) : 9.999999933815812510711506376258e+36f)) * ((abs(_436) > 0.0f) ? (1.0f / _436) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f);
    float _463 = clamp(clamp(_303 - 0.5f, 0.0f, 1.0f) + asfloat((asuint(_328.w) & cb3_m[44u].w) | cb3_m[45u].w), 0.0f, 1.0f);
    float _468 = clamp(mad(_445, 0.89999997615814208984375f, 0.100000001490116119384765625f), 0.0f, 1.0f);
    o0.w = _445;
    o0.x = ((((_361 * (_387 * asfloat((asuint(_396.x) & cb3_m[50u].x) | cb3_m[51u].x))) * _445) + (((_303 * _387) * _445) * asfloat((asuint(_328.x) & cb3_m[44u].x) | cb3_m[45u].x))) * _463) * _468;
    o0.y = (((((_303 * _388) * _445) * asfloat((asuint(_328.y) & cb3_m[44u].y) | cb3_m[45u].y)) + ((_361 * (asfloat(cb3_m[51u].y | (asuint(_396.y) & cb3_m[50u].y)) * _388)) * _445)) * _463) * _468;
    o0.z = (((((_303 * _389) * _445) * asfloat((asuint(_328.z) & cb3_m[44u].z) | cb3_m[45u].z)) + ((_361 * (_389 * asfloat((asuint(_396.z) & cb3_m[50u].z) | cb3_m[51u].z))) * _445)) * _463) * _468;
}