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
    precise float _89 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _89));
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
    float _107 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _120 = asfloat(cb4_m[210u].x);
    float _127 = asfloat(cb4_m[210u].y);
    float _132 = mad((abs(_120) > 0.0f) ? (1.0f / _120) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _107, 0.5f, 0.5f));
    float _133 = mad((abs(_127) > 0.0f) ? (1.0f / _127) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _107, -0.5f, 0.5f));
    float _136 = asfloat(cb4_m[12u].x);
    float _140 = (abs(_136) > 0.0f) ? (1.0f / _136) : 9.999999933815812510711506376258e+36f;
    float2 _146 = float2(_132, _133);
    float _169 = mad(asfloat((asuint(t5.Sample(s5, _146).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _172 = asfloat(cb4_m[13u].y);
    float _176 = (abs(_172) > 0.0f) ? (1.0f / _172) : 9.999999933815812510711506376258e+36f;
    float _177 = _169 * mad(_132, 2.0f, -1.0f);
    float _178 = mad(_133, -2.0f, 1.0f) * _169;
    float _179 = _177 * _140;
    float _180 = _176 * _178;
    float3 _183 = float3(-_179, -_180, _169);
    float _186 = rsqrt(abs(dp3_f32(_183, _183)));
    float _189 = (asuint(_186) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _186;
    float _195 = asfloat(cb4_m[194u].x);
    float _196 = asfloat(cb4_m[194u].y);
    float _197 = asfloat(cb4_m[194u].z);
    float _199 = mad(-_179, _189, _195);
    float _201 = mad(-_189, _180, _196);
    float _202 = mad(_169, _189, _197);
    float3 _203 = float3(_199, _201, _202);
    float _205 = rsqrt(dp3_f32(_203, _203));
    float _208 = (asuint(_205) != 2139095040u) ? _205 : 0.0f;
    float4 _215 = t1.Sample(s1, _146);
    float _259 = mad(-_177, _140, asfloat(cb4_m[192u].x));
    float _261 = mad(-_176, _178, asfloat(cb4_m[192u].y));
    float _262 = _169 + asfloat(cb4_m[192u].z);
    float3 _264 = float3(mad(asfloat(cb3_m[47u].x | (cb3_m[46u].x & asuint(_215.x))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (asuint(_215.y) & cb3_m[46u].y)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_215.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float3 _266 = float3(_195, _196, _197);
    float _268 = clamp(dp3_f32(_266, _264), 0.0f, 1.0f);
    float4 _273 = t2.Sample(s2, _146);
    float4 _293 = t0.Sample(s0, _146);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _293.rgb = gamma_sRGB_to_linear(_293.rgb, GCT_MIRROR);
#endif
    float _322 = asfloat(cb3_m[49u].w | (asuint(_273.w) & cb3_m[48u].w));
    float _324 = 1.0f - _322;
    float _326 = asfloat(cb3_m[49u].z | (asuint(_273.z) & cb3_m[48u].z)) * 5.0f;
    float _327 = asfloat(cb3_m[47u].w | (cb3_m[46u].w & asuint(_215.w)));
    float _340 = clamp((((0.49500000476837158203125f - _327) >= 0.0f) ? 0.0f : 1.0f) - (((_327 - 0.49500000476837158203125f) >= 0.0f) ? 0.0f : 1.0f), 0.0f, 1.0f);
    float _343 = mad(_340, -0.5f, _327);
    float _352 = _340 * asfloat(cb4_m[193u].x);
    float _353 = _340 * asfloat(cb4_m[193u].y);
    float _354 = _340 * asfloat(cb4_m[193u].z);
    float4 _361 = t3.Sample(s3, float2(clamp((max(dp3_f32(float3(_199 * _208, _208 * _201, _208 * _202), _264), 0.0f) - _322) * clamp((abs(_324) > 0.0f) ? (1.0f / _324) : 9.999999933815812510711506376258e+36f, 0.0f, 1.0f), 0.0f, 1.0f), clamp(_343 + _343, 0.0f, 1.0f)));
    float3 _384 = float3(_259, _261, _262);
    float _387 = rsqrt(abs(dp3_f32(_384, _384)));
    float _390 = (asuint(_387) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _387;
    float _395 = dp3_f32(float3(_259 * _390, _261 * _390, _262 * _390), _266);
    float _399 = _395 - asfloat(cb4_m[195u].x);
    float _413 = ((-clamp(_399, 0.0f, 1.0f)) >= 0.0f) ? 0.0f : 1.0f;
    float _418 = _413 * _268;
    float _447 = clamp(((((_418 * 0.5f) + (_395 * 0.5f)) * mad(_413, _268, frac(-_418))) * clamp((_399 * asfloat(cb4_m[194u].w)) * asfloat(cb4_m[196u].y), 0.0f, 1.0f)) * clamp((1.0f - clamp(asfloat(cb4_m[196u].z) * ((abs(_390) > 0.0f) ? (1.0f / _390) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f)) * asfloat(cb4_m[196u].w), 0.0f, 1.0f), 0.0f, 1.0f);
    float _465 = clamp(_447 + asfloat((asuint(_293.w) & cb3_m[44u].w) | cb3_m[45u].w), 0.0f, 1.0f);
    o0.w = _447;
    o0.x = (((_326 * (_352 * asfloat((cb3_m[50u].x & asuint(_361.x)) | cb3_m[51u].x))) * _447) + ((_352 * _447) * asfloat((asuint(_293.x) & cb3_m[44u].x) | cb3_m[45u].x))) * _465;
    o0.y = (((_353 * _447) * asfloat((asuint(_293.y) & cb3_m[44u].y) | cb3_m[45u].y)) + ((_326 * (asfloat(cb3_m[51u].y | (asuint(_361.y) & cb3_m[50u].y)) * _353)) * _447)) * _465;
    o0.z = (((_354 * _447) * asfloat((asuint(_293.z) & cb3_m[44u].z) | cb3_m[45u].z)) + ((_326 * (asfloat(cb3_m[51u].z | (asuint(_361.z) & cb3_m[50u].z)) * _354)) * _447)) * _465;
}