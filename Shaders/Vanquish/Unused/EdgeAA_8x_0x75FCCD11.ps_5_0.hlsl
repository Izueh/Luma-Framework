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
    float _108 = asfloat(cb4_m[210u].y);
    float _114 = (abs(_108) > 0.0f) ? (1.0f / _108) : 9.999999933815812510711506376258e+36f;
    float _117 = asfloat(cb4_m[210u].x);
    float _121 = (abs(_117) > 0.0f) ? (1.0f / _117) : 9.999999933815812510711506376258e+36f;
    float _122 = v5.x - _121;
    float _123 = v5.y + _114;
    float _124 = v5.x + _121;
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
    float _238 = v5.y - _114;
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
    float _322 = mad(_121, -0.0f, v5.y);
    float2 _324 = float2(mad(_121, -1.0f, v5.x), _322);
    float4 _326 = t0.Sample(s0, _324);
    bool _360 = ((_177 - asfloat((asuint(t1.Sample(s1, _324).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _361 = _360 ? _317 : (asfloat((asuint(_326.x) & cb3_m[44u].x) | cb3_m[45u].x) + _317);
    float _362 = _360 ? _318 : (asfloat((asuint(_326.y) & cb3_m[44u].y) | cb3_m[45u].y) + _318);
    float _363 = _360 ? _319 : (asfloat((asuint(_326.z) & cb3_m[44u].z) | cb3_m[45u].z) + _319);
    float _364 = _360 ? _320 : (asfloat((asuint(_326.w) & cb3_m[44u].w) | cb3_m[45u].w) + _320);
    float2 _365 = float2(mad(_121, 1.0f, v5.x), _322);
    float4 _367 = t0.Sample(s0, _365);
    bool _401 = ((_177 - asfloat((asuint(t1.Sample(s1, _365).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _402 = _401 ? _361 : (asfloat((asuint(_367.x) & cb3_m[44u].x) | cb3_m[45u].x) + _361);
    float _403 = _401 ? _362 : (asfloat((asuint(_367.y) & cb3_m[44u].y) | cb3_m[45u].y) + _362);
    float _404 = _401 ? _363 : (asfloat((asuint(_367.z) & cb3_m[44u].z) | cb3_m[45u].z) + _363);
    float _405 = _401 ? _364 : (asfloat((asuint(_367.w) & cb3_m[44u].w) | cb3_m[45u].w) + _364);
    float _406 = mad(_121, -0.0f, v5.x);
    float2 _409 = float2(_406, mad(_114, 1.0f, v5.y));
    float4 _411 = t0.Sample(s0, _409);
    bool _445 = ((_177 - asfloat((asuint(t1.Sample(s1, _409).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _446 = _445 ? _402 : (asfloat((asuint(_411.x) & cb3_m[44u].x) | cb3_m[45u].x) + _402);
    float _447 = _445 ? _403 : (asfloat((asuint(_411.y) & cb3_m[44u].y) | cb3_m[45u].y) + _403);
    float _448 = _445 ? _404 : (asfloat((asuint(_411.z) & cb3_m[44u].z) | cb3_m[45u].z) + _404);
    float _449 = _445 ? _405 : (asfloat((asuint(_411.w) & cb3_m[44u].w) | cb3_m[45u].w) + _405);
    float2 _450 = float2(_406, mad(_114, -1.0f, v5.y));
    float4 _452 = t0.Sample(s0, _450);
    bool _486 = ((_177 - asfloat((asuint(t1.Sample(s1, _450).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _487 = _486 ? _446 : (asfloat((asuint(_452.x) & cb3_m[44u].x) | cb3_m[45u].x) + _446);
    float _488 = _486 ? _447 : (asfloat((asuint(_452.y) & cb3_m[44u].y) | cb3_m[45u].y) + _447);
    float _489 = _486 ? _448 : (asfloat((asuint(_452.z) & cb3_m[44u].z) | cb3_m[45u].z) + _448);
    float _490 = _486 ? _449 : (asfloat((asuint(_452.w) & cb3_m[44u].w) | cb3_m[45u].w) + _449);
    float _491 = mad(_121, -0.5f, v5.x);
    float _492 = mad(_114, 0.5f, v5.y);
    float _493 = mad(_121, 0.5f, v5.x);
    float2 _494 = float2(_491, _492);
    float4 _496 = t0.Sample(s0, _494);
    bool _530 = ((_177 - asfloat((asuint(t1.Sample(s1, _494).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _531 = _530 ? _487 : (asfloat((asuint(_496.x) & cb3_m[44u].x) | cb3_m[45u].x) + _487);
    float _532 = _530 ? _488 : (asfloat((asuint(_496.y) & cb3_m[44u].y) | cb3_m[45u].y) + _488);
    float _533 = _530 ? _489 : (asfloat((asuint(_496.z) & cb3_m[44u].z) | cb3_m[45u].z) + _489);
    float _534 = _530 ? _490 : (asfloat((asuint(_496.w) & cb3_m[44u].w) | cb3_m[45u].w) + _490);
    float2 _535 = float2(_493, _492);
    float4 _537 = t0.Sample(s0, _535);
    bool _571 = ((_177 - asfloat((asuint(t1.Sample(s1, _535).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _572 = _571 ? _531 : (asfloat((asuint(_537.x) & cb3_m[44u].x) | cb3_m[45u].x) + _531);
    float _573 = _571 ? _532 : (asfloat((asuint(_537.y) & cb3_m[44u].y) | cb3_m[45u].y) + _532);
    float _574 = _571 ? _533 : (asfloat((asuint(_537.z) & cb3_m[44u].z) | cb3_m[45u].z) + _533);
    float _575 = _571 ? _534 : (asfloat((asuint(_537.w) & cb3_m[44u].w) | cb3_m[45u].w) + _534);
    float _576 = mad(_114, -0.5f, v5.y);
    float2 _577 = float2(_491, _576);
    float4 _579 = t0.Sample(s0, _577);
    bool _613 = ((_177 - asfloat((asuint(t1.Sample(s1, _577).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _614 = _613 ? _572 : (asfloat((asuint(_579.x) & cb3_m[44u].x) | cb3_m[45u].x) + _572);
    float _615 = _613 ? _573 : (asfloat((asuint(_579.y) & cb3_m[44u].y) | cb3_m[45u].y) + _573);
    float _616 = _613 ? _574 : (asfloat((asuint(_579.z) & cb3_m[44u].z) | cb3_m[45u].z) + _574);
    float _617 = _613 ? _575 : (asfloat((asuint(_579.w) & cb3_m[44u].w) | cb3_m[45u].w) + _575);
    float2 _618 = float2(_493, _576);
    float4 _620 = t0.Sample(s0, _618);
    bool _654 = ((_177 - asfloat((asuint(t1.Sample(s1, _618).x) & cb3_m[46u].x) | cb3_m[47u].x)) + _186) >= 0.0f;
    float _655 = _654 ? _614 : (asfloat((asuint(_620.x) & cb3_m[44u].x) | cb3_m[45u].x) + _614);
    float _656 = _654 ? _615 : (asfloat((asuint(_620.y) & cb3_m[44u].y) | cb3_m[45u].y) + _615);
    float _657 = _654 ? _616 : (asfloat((asuint(_620.z) & cb3_m[44u].z) | cb3_m[45u].z) + _616);
    float _658 = _654 ? _617 : (asfloat((asuint(_620.w) & cb3_m[44u].w) | cb3_m[45u].w) + _617);
    float2 _659 = float2(_491, _322);
    float4 _661 = t0.Sample(s0, _659);
    bool _695 = (_186 + (_177 - asfloat((asuint(t1.Sample(s1, _659).x) & cb3_m[46u].x) | cb3_m[47u].x))) >= 0.0f;
    float _696 = _695 ? _655 : (asfloat((asuint(_661.x) & cb3_m[44u].x) | cb3_m[45u].x) + _655);
    float _697 = _695 ? _656 : (asfloat((asuint(_661.y) & cb3_m[44u].y) | cb3_m[45u].y) + _656);
    float _698 = _695 ? _657 : (asfloat((asuint(_661.z) & cb3_m[44u].z) | cb3_m[45u].z) + _657);
    float _699 = _695 ? _658 : (asfloat((asuint(_661.w) & cb3_m[44u].w) | cb3_m[45u].w) + _658);
    float2 _700 = float2(_493, _322);
    float4 _702 = t0.Sample(s0, _700);
    bool _736 = (_186 + (_177 - asfloat((asuint(t1.Sample(s1, _700).x) & cb3_m[46u].x) | cb3_m[47u].x))) >= 0.0f;
    float _737 = _736 ? _696 : (asfloat((asuint(_702.x) & cb3_m[44u].x) | cb3_m[45u].x) + _696);
    float _738 = _736 ? _697 : (asfloat((asuint(_702.y) & cb3_m[44u].y) | cb3_m[45u].y) + _697);
    float _739 = _736 ? _698 : (asfloat((asuint(_702.z) & cb3_m[44u].z) | cb3_m[45u].z) + _698);
    float _740 = _736 ? _699 : (asfloat((asuint(_702.w) & cb3_m[44u].w) | cb3_m[45u].w) + _699);
    float2 _741 = float2(_406, _492);
    float4 _743 = t0.Sample(s0, _741);
    bool _777 = (_186 + (_177 - asfloat((asuint(t1.Sample(s1, _741).x) & cb3_m[46u].x) | cb3_m[47u].x))) >= 0.0f;
    float _778 = _777 ? _737 : (asfloat((asuint(_743.x) & cb3_m[44u].x) | cb3_m[45u].x) + _737);
    float _779 = _777 ? _738 : (asfloat((asuint(_743.y) & cb3_m[44u].y) | cb3_m[45u].y) + _738);
    float _780 = _777 ? _739 : (_739 + asfloat((asuint(_743.z) & cb3_m[44u].z) | cb3_m[45u].z));
    float _781 = _777 ? _740 : (asfloat((asuint(_743.w) & cb3_m[44u].w) | cb3_m[45u].w) + _740);
    float2 _782 = float2(_406, _576);
    float4 _784 = t0.Sample(s0, _782);
    bool _818 = (_186 + (_177 - asfloat((asuint(t1.Sample(s1, _782).x) & cb3_m[46u].x) | cb3_m[47u].x))) >= 0.0f;
    float _828 = _275 ? _237 : (_237 + 1.0f);
    float _830 = _316 ? _828 : (_828 + 1.0f);
    float _832 = _360 ? _830 : (_830 + 1.0f);
    float _834 = _401 ? _832 : (_832 + 1.0f);
    float _836 = _445 ? _834 : (_834 + 1.0f);
    float _838 = _486 ? _836 : (_836 + 1.0f);
    float _840 = _530 ? _838 : (_838 + 1.0f);
    float _842 = _571 ? _840 : (_840 + 1.0f);
    float _844 = _613 ? _842 : (_842 + 1.0f);
    float _846 = _654 ? _844 : (_844 + 1.0f);
    float _848 = _695 ? _846 : (_846 + 1.0f);
    float _850 = _736 ? _848 : (_848 + 1.0f);
    float _852 = _777 ? _850 : (_850 + 1.0f);
    float _855 = _183 ? (_818 ? _852 : (_852 + 1.0f)) : _838;
    float _859 = (abs(_855) > 0.0f) ? (1.0f / _855) : 9.999999933815812510711506376258e+36f;
    bool _876 = ((((((v5.y - 0.999000012874603271484375f) >= 0.0f) || ((0.001000000047497451305389404296875f - v5.y) >= 0.0f)) || ((v5.x - 0.999000012874603271484375f) >= 0.0f)) || ((0.001000000047497451305389404296875f - v5.x) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f;
    o0.x = (_876 ? _144 : ((_183 ? (_818 ? _778 : (_778 + asfloat((asuint(_784.x) & cb3_m[44u].x) | cb3_m[45u].x))) : _487) * _859)) * asfloat(cb4_m[192u].x);
    o0.y = (_876 ? _145 : ((_183 ? (_818 ? _779 : (asfloat((asuint(_784.y) & cb3_m[44u].y) | cb3_m[45u].y) + _779)) : _488) * _859)) * asfloat(cb4_m[192u].y);
    o0.z = (_876 ? _146 : ((_183 ? (_818 ? _780 : (asfloat((asuint(_784.z) & cb3_m[44u].z) | cb3_m[45u].z) + _780)) : _489) * _859)) * asfloat(cb4_m[192u].z);
    o0.w = (_876 ? _147 : ((_183 ? (_818 ? _781 : (asfloat((asuint(_784.w) & cb3_m[44u].w) | cb3_m[45u].w) + _781)) : _490) * _859)) * asfloat(cb4_m[192u].w);
}