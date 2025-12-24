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
    float2 _70 = float2(v5.x, v5.y);
    float4 _73 = t0.Sample(s0, _70);
    float _108 = asfloat(cb4_m[210u].x);
    float _114 = (abs(_108) > 0.0f) ? (1.0f / _108) : 9.999999933815812510711506376258e+36f;
    float _117 = asfloat(cb4_m[210u].y);
    float _121 = (abs(_117) > 0.0f) ? (1.0f / _117) : 9.999999933815812510711506376258e+36f;
    float _122 = v5.x - _114;
    float _123 = v5.y + _121;
    float _124 = v5.x + _114;
    float2 _125 = float2(_122, _123);
    float4 _127 = t0.Sample(s0, _125);
    float _144 = asfloat((asuint(_73.x) & cb3_m[44u].x) | cb3_m[45u].x);
    float _145 = asfloat((asuint(_73.y) & cb3_m[44u].y) | cb3_m[45u].y);
    float _146 = asfloat((asuint(_73.z) & cb3_m[44u].z) | cb3_m[45u].z);
    float _147 = asfloat((asuint(_73.w) & cb3_m[44u].w) | cb3_m[45u].w);
    float _177 = asfloat((asuint(t1.Sample(s1, _70).x) & cb3_m[46u].x) | cb3_m[47u].x);
    bool _183 = (0.00200000009499490261077880859375f - _177) >= 0.0f;
    float _186 = ((0.00039999998989515006542205810546875f - _177) >= 0.0f) ? 9.9999997473787516355514526367188e-06f : (_183 ? 0.00015000000712461769580841064453125f : (((0.039999999105930328369140625f - _177) >= 0.0f) ? 0.0003000000142492353916168212890625f : 0.00200000009499490261077880859375f));
    bool _189 = ((_177 - asfloat((asuint(t1.Sample(s1, _125).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _190 = _189 ? _144 : (_144 + asfloat((asuint(_127.x) & cb3_m[44u].x) | cb3_m[45u].x));
    float _191 = _189 ? _145 : (_145 + asfloat((asuint(_127.y) & cb3_m[44u].y) | cb3_m[45u].y));
    float _192 = _189 ? _146 : (_146 + asfloat((asuint(_127.z) & cb3_m[44u].z) | cb3_m[45u].z));
    float _193 = _189 ? _147 : (_147 + asfloat((asuint(_127.w) & cb3_m[44u].w) | cb3_m[45u].w));
    float2 _196 = float2(_124, _123);
    float4 _198 = t0.Sample(s0, _196);
    bool _232 = ((_177 - asfloat((asuint(t1.Sample(s1, _196).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _233 = _232 ? _190 : (asfloat((asuint(_198.x) & cb3_m[44u].x) | cb3_m[45u].x) + _190);
    float _234 = _232 ? _191 : (asfloat(cb3_m[45u].y | (asuint(_198.y) & cb3_m[44u].y)) + _191);
    float _235 = _232 ? _192 : (asfloat(cb3_m[45u].z | (asuint(_198.z) & cb3_m[44u].z)) + _192);
    float _236 = _232 ? _193 : (asfloat(cb3_m[45u].w | (asuint(_198.w) & cb3_m[44u].w)) + _193);
    float _237 = _232 ? (_189 ? 1.0f : 2.0f) : (_189 ? 2.0f : 3.0f);
    float _238 = v5.y - _121;
    float2 _239 = float2(_122, _238);
    float4 _241 = t0.Sample(s0, _239);
    bool _275 = ((_177 - asfloat((asuint(t1.Sample(s1, _239).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _276 = _275 ? _233 : (asfloat((asuint(_241.x) & cb3_m[44u].x) | cb3_m[45u].x) + _233);
    float _277 = _275 ? _234 : (asfloat((asuint(_241.y) & cb3_m[44u].y) | cb3_m[45u].y) + _234);
    float _278 = _275 ? _235 : (asfloat((asuint(_241.z) & cb3_m[44u].z) | cb3_m[45u].z) + _235);
    float _279 = _275 ? _236 : (asfloat((asuint(_241.w) & cb3_m[44u].w) | cb3_m[45u].w) + _236);
    float2 _280 = float2(_124, _238);
    float4 _282 = t0.Sample(s0, _280);
    bool _316 = ((_177 - asfloat((asuint(t1.Sample(s1, _280).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _317 = _316 ? _276 : (asfloat((asuint(_282.x) & cb3_m[44u].x) | cb3_m[45u].x) + _276);
    float _318 = _316 ? _277 : (asfloat((asuint(_282.y) & cb3_m[44u].y) | cb3_m[45u].y) + _277);
    float _319 = _316 ? _278 : (asfloat((asuint(_282.z) & cb3_m[44u].z) | cb3_m[45u].z) + _278);
    float _320 = _316 ? _279 : (asfloat((asuint(_282.w) & cb3_m[44u].w) | cb3_m[45u].w) + _279);
    float _321 = mad(_114, -0.5f, v5.x);
    float _322 = mad(_121, 0.5f, v5.y);
    float _323 = mad(_114, 0.5f, v5.x);
    float _324 = mad(_121, -0.5f, v5.y);
    float2 _325 = float2(_321, _322);
    float4 _327 = t0.Sample(s0, _325);
    bool _361 = ((_177 - asfloat((asuint(t1.Sample(s1, _325).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _362 = _361 ? _317 : (asfloat((asuint(_327.x) & cb3_m[44u].x) | cb3_m[45u].x) + _317);
    float _363 = _361 ? _318 : (asfloat((asuint(_327.y) & cb3_m[44u].y) | cb3_m[45u].y) + _318);
    float _364 = _361 ? _319 : (asfloat((asuint(_327.z) & cb3_m[44u].z) | cb3_m[45u].z) + _319);
    float _365 = _361 ? _320 : (asfloat((asuint(_327.w) & cb3_m[44u].w) | cb3_m[45u].w) + _320);
    float2 _366 = float2(_323, _322);
    float4 _368 = t0.Sample(s0, _366);
    bool _402 = ((_177 - asfloat((asuint(t1.Sample(s1, _366).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _403 = _402 ? _362 : (asfloat((asuint(_368.x) & cb3_m[44u].x) | cb3_m[45u].x) + _362);
    float _404 = _402 ? _363 : (asfloat((asuint(_368.y) & cb3_m[44u].y) | cb3_m[45u].y) + _363);
    float _405 = _402 ? _364 : (asfloat((asuint(_368.z) & cb3_m[44u].z) | cb3_m[45u].z) + _364);
    float _406 = _402 ? _365 : (asfloat((asuint(_368.w) & cb3_m[44u].w) | cb3_m[45u].w) + _365);
    float2 _407 = float2(_321, _324);
    float4 _409 = t0.Sample(s0, _407);
    bool _443 = (_186 + (_177 - asfloat(cb3_m[47u].x | (asuint(t1.Sample(s1, _407).x) & cb3_m[46u].x)))) >= 0.0f;
    float _444 = _443 ? _403 : (asfloat((asuint(_409.x) & cb3_m[44u].x) | cb3_m[45u].x) + _403);
    float _445 = _443 ? _404 : (asfloat((asuint(_409.y) & cb3_m[44u].y) | cb3_m[45u].y) + _404);
    float _446 = _443 ? _405 : (asfloat((asuint(_409.z) & cb3_m[44u].z) | cb3_m[45u].z) + _405);
    float _447 = _443 ? _406 : (asfloat((asuint(_409.w) & cb3_m[44u].w) | cb3_m[45u].w) + _406);
    float2 _448 = float2(_323, _324);
    float4 _450 = t0.Sample(s0, _448);
    bool _484 = (_186 + (_177 - asfloat(cb3_m[47u].x | (asuint(t1.Sample(s1, _448).x) & cb3_m[46u].x)))) >= 0.0f;
    float _494 = _275 ? _237 : (_237 + 1.0f);
    float _496 = _316 ? _494 : (_494 + 1.0f);
    float _498 = _361 ? _496 : (_496 + 1.0f);
    float _500 = _402 ? _498 : (_498 + 1.0f);
    float _502 = _443 ? _500 : (_500 + 1.0f);
    float _505 = _183 ? (_484 ? _502 : (_502 + 1.0f)) : _496;
    float _509 = (abs(_505) > 0.0f) ? (1.0f / _505) : 9.999999933815812510711506376258e+36f;
    bool _526 = ((((((v5.y - 0.999000012874603271484375f) >= 0.0f) || ((0.001000000047497451305389404296875f - v5.y) >= 0.0f)) || ((v5.x - 0.999000012874603271484375f) >= 0.0f)) || ((0.001000000047497451305389404296875f - v5.x) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f;
    o0.x = (_526 ? _144 : ((_183 ? (_484 ? _444 : (_444 + asfloat((asuint(_450.x) & cb3_m[44u].x) | cb3_m[45u].x))) : _317) * _509)) * asfloat(cb4_m[192u].x);
    o0.y = (_526 ? _145 : ((_183 ? (_484 ? _445 : (_445 + asfloat((asuint(_450.y) & cb3_m[44u].y) | cb3_m[45u].y))) : _318) * _509)) * asfloat(cb4_m[192u].y);
    o0.z = (_526 ? _146 : ((_183 ? (_484 ? _446 : (_446 + asfloat((asuint(_450.z) & cb3_m[44u].z) | cb3_m[45u].z))) : _319) * _509)) * asfloat(cb4_m[192u].z);
    o0.w = (_526 ? _147 : ((_183 ? (_484 ? _447 : (_447 + asfloat((asuint(_450.w) & cb3_m[44u].w) | cb3_m[45u].w))) : _320) * _509)) * asfloat(cb4_m[192u].w);
}