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
    float _67 = (abs(v6.w) > 0.0f) ? (1.0f / v6.w) : 9.999999933815812510711506376258e+36f;
    float _81 = asfloat(cb4_m[210u].x);
    float _88 = asfloat(cb4_m[210u].y);
    float _125 = asfloat(cb4_m[193u].x);
    float4 _146 = t0.Sample(s0, float2(v5.x, v5.y));
    o0.x = asfloat((asuint(_146.x) & cb3_m[44u].x) | cb3_m[45u].x) * asfloat(cb4_m[192u].x);
    o0.y = asfloat(cb3_m[45u].y | (asuint(_146.y) & cb3_m[44u].y)) * asfloat(cb4_m[192u].y);
    o0.z = asfloat(cb3_m[45u].z | (asuint(_146.z) & cb3_m[44u].z)) * asfloat(cb4_m[192u].z);
    float _202 = mad(clamp((mad(asfloat(cb3_m[47u].x | (asuint(t1.Sample(s1, float2(mad((abs(_81) > 0.0f) ? (1.0f / _81) : 9.999999933815812510711506376258e+36f, 0.5f, mad(v6.x * _67, 0.5f, 0.5f)), mad((abs(_88) > 0.0f) ? (1.0f / _88) : 9.999999933815812510711506376258e+36f, 0.5f, 1.0f - mad(v6.y * _67, 0.5f, 0.5f)))).x) & cb3_m[46u].x)), asfloat(cb4_m[201u].y), asfloat(cb4_m[201u].x)) - v6.w) * ((abs(_125) > 0.0f) ? (1.0f / _125) : 9.999999933815812510711506376258e+36f), 0.0f, 1.0f), asfloat(cb3_m[45u].w | (asuint(_146.w) & cb3_m[44u].w)) * asfloat(cb4_m[192u].w), asfloat(cb4_m[192u].w) - asfloat(cb4_m[194u].w)) * asfloat(cb4_m[194u].x);
    o0.w = _202;
    o0.w = saturate(o0.w); // Luma: emulate UNORM
}