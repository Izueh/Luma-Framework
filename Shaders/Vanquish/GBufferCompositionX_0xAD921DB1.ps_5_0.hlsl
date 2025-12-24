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
SamplerState s3 : register(s3);
SamplerState s5 : register(s5);
SamplerState s6 : register(s6);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t6 : register(t6);

float dp3_f32(float3 a, float3 b)
{
    precise float _93 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _93));
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
    float _111 = (abs(v5.w) > 0.0f) ? (1.0f / v5.w) : 9.999999933815812510711506376258e+36f;
    float _124 = asfloat(cb4_m[210u].x);
    float _131 = asfloat(cb4_m[210u].y);
    float _136 = mad((abs(_124) > 0.0f) ? (1.0f / _124) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.x * _111, 0.5f, 0.5f));
    float _137 = mad((abs(_131) > 0.0f) ? (1.0f / _131) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v5.y * _111, -0.5f, 0.5f));
    float _140 = asfloat(cb4_m[12u].x);
    float _144 = (abs(_140) > 0.0f) ? (1.0f / _140) : 9.999999933815812510711506376258e+36f;
    float2 _150 = float2(_136, _137);
    float _173 = mad(asfloat((asuint(t5.Sample(s5, _150).x) & cb3_m[54u].x) | cb3_m[55u].x), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x));
    float _176 = asfloat(cb4_m[13u].y);
    float _180 = (abs(_176) > 0.0f) ? (1.0f / _176) : 9.999999933815812510711506376258e+36f;
    float _181 = mad(_136, 2.0f, -1.0f) * _173;
    float _182 = mad(_137, -2.0f, 1.0f) * _173;
    float _183 = _181 * _144;
    float _184 = _180 * _182;
    float3 _187 = float3(-_183, -_184, _173);
    float _190 = rsqrt(abs(dp3_f32(_187, _187)));
    float _193 = (asuint(_190) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _190;
    float _199 = asfloat(cb4_m[194u].x);
    float _200 = asfloat(cb4_m[194u].y);
    float _201 = asfloat(cb4_m[194u].z);
    float _203 = mad(-_183, _193, _199);
    float _205 = mad(-_193, _184, _200);
    float _206 = mad(_173, _193, _201);
    float3 _207 = float3(_203, _205, _206);
    float _209 = rsqrt(dp3_f32(_207, _207));
    float _212 = (asuint(_209) != 2139095040u) ? _209 : 0.0f;
    float4 _219 = t1.Sample(s1, _150);
    float _263 = mad(-_181, _144, asfloat(cb4_m[192u].x));
    float _265 = mad(-_180, _182, asfloat(cb4_m[192u].y));
    float _266 = _173 + asfloat(cb4_m[192u].z);
    float3 _268 = float3(mad(asfloat(cb3_m[47u].x | (cb3_m[46u].x & asuint(_219.x))), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (asuint(_219.y) & cb3_m[46u].y)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_219.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float3 _270 = float3(_199, _200, _201);
    float4 _277 = t2.Sample(s2, _150);
    float _294 = asfloat(cb3_m[49u].w | (asuint(_277.w) & cb3_m[48u].w));
    float _296 = 1.0f - _294;
    float _297 = asfloat(cb3_m[47u].w | (cb3_m[46u].w & asuint(_219.w)));
    float _305 = asfloat(cb3_m[49u].z | (asuint(_277.z) & cb3_m[48u].z)) * 5.0f;
    float _313 = mad(clamp((((0.49500000476837158203125f - _297) >= 0.0f) ? 0.0f : 1.0f) - (((_297 - 0.49500000476837158203125f) >= 0.0f) ? 0.0f : 1.0f), 0.0f, 1.0f), -0.5f, _297);
    float4 _322 = t3.Sample(s3, float2(clamp((max(dp3_f32(float3(_203 * _212, _212 * _205, _212 * _206), _268), 0.0f) - _294) * clamp((abs(_296) > 0.0f) ? (1.0f / _296) : 9.999999933815812510711506376258e+36f, 0.0f, 1.0f), 0.0f, 1.0f), clamp(_313 + _313, 0.0f, 1.0f)));
    float4 _362 = t0.Sample(s0, _150);
#if 1 // Emulate R8G8B8A8_UNORM_SRGB view with upgraded R16G16B16A16_FLOAT textures
    _362.rgb = gamma_sRGB_to_linear(_362.rgb, GCT_MIRROR);
#endif
    float3 _391 = float3(_263, _265, _266);
    float _394 = rsqrt(abs(dp3_f32(_391, _391)));
    float _397 = (asuint(_394) == 2139095040u) ? 9.999999933815812510711506376258e+36f : _394;
    float _402 = dp3_f32(float3(_263 * _397, _265 * _397, _266 * _397), _270);
    float _406 = _402 - asfloat(cb4_m[195u].x);
    float _407 = asfloat(cb3_m[57u].w | (asuint(t6.Sample(s6, _150).w) & cb3_m[56u].w));
    float _416 = _407 * asfloat(cb4_m[193u].x);
    float _417 = _407 * asfloat(cb4_m[193u].y);
    float _418 = _407 * asfloat(cb4_m[193u].z);
    float _462 = clamp(((((clamp(dp3_f32(_270, _268), 0.0f, 1.0f) * (((-clamp(_406, 0.0f, 1.0f)) >= 0.0f) ? 0.0f : 1.0f)) * 0.5f) + (_402 * 0.5f)) * clamp((_406 * asfloat(cb4_m[194u].w)) * asfloat(cb4_m[196u].y), 0.0f, 1.0f)) * clamp((1.0f - clamp(asfloat(cb4_m[196u].z) * ((abs(_397) > 0.0f) ? (1.0f / _397) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f)) * asfloat(cb4_m[196u].w), 0.0f, 1.0f), 0.0f, 1.0f);
    float _480 = clamp(_462 + asfloat(cb3_m[45u].w | (asuint(_362.w) & cb3_m[44u].w)), 0.0f, 1.0f);
    o0.w = _462;
    o0.x = (((_305 * (_416 * asfloat((asuint(_322.x) & cb3_m[50u].x) | cb3_m[51u].x))) * _462) + ((_416 * _462) * asfloat((asuint(_362.x) & cb3_m[44u].x) | cb3_m[45u].x))) * _480;
    o0.y = (((_417 * _462) * asfloat(cb3_m[45u].y | (cb3_m[44u].y & asuint(_362.y)))) + ((_305 * (asfloat((asuint(_322.y) & cb3_m[50u].y) | cb3_m[51u].y) * _417)) * _462)) * _480;
    o0.z = (((_418 * _462) * asfloat(cb3_m[45u].z | (asuint(_362.z) & cb3_m[44u].z))) + ((_305 * (asfloat((asuint(_322.z) & cb3_m[50u].z) | cb3_m[51u].z) * _418)) * _462)) * _480;
}