cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[50] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[215] : packoffset(c0);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

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
    float _104 = asfloat(cb4_m[214u].y);
    float _105 = frac(_104);
    float _113 = (_104 - _105) + ((((-_105) >= 0.0f) || (_104 >= 0.0f)) ? 0.0f : 1.0f);
    float2 _122 = float2(v5.x, v5.y);
    float _138 = asfloat((asuint(t2.Sample(s2, _122).x) & cb3_m[48u].x) | cb3_m[49u].x);
    float _141 = asfloat(cb4_m[201u].y);
    float _142 = _138 * _141;
    float _143 = _113 * _142;
    float _145 = clamp(mad(_142, -0.012500000186264514923095703125f, 1.0f), 0.0f, 1.0f);
    float _147 = frac(_143 * 0.012500000186264514923095703125f);
    float _156 = _113 - (mad(_143, 0.012500000186264514923095703125f, -_147) + ((((-_147) >= 0.0f) || (_143 >= 0.0f)) ? 0.0f : 1.0f));
    float _181 = ((-abs(_156 - 5.0f)) >= 0.0f) ? 16.0f : (((-abs(_156 - 4.0f)) >= 0.0f) ? 12.0f : (((-abs(_156 - 3.0f)) >= 0.0f) ? 8.0f : (((-abs(_156 - 2.0f)) >= 0.0f) ? 4.0f : (((-abs(_156 - 1.0f)) >= 0.0f) ? 2.0f : 0.0f))));
    float _187 = _181 + _181;
    float _191 = (abs(_187) > 0.0f) ? (1.0f / _187) : 9.999999933815812510711506376258e+36f;
    float _196 = asfloat(cb4_m[210u].x);
    float _200 = (abs(_196) > 0.0f) ? (1.0f / _196) : 9.999999933815812510711506376258e+36f;
    float _207 = _145 * asfloat(cb4_m[214u].x);
    float _209 = _207 * _200;
    float _210 = mad(_207, _200, v5.x);
    float _211 = v5.y + 0.0f;
    float _213 = mad(-_207, _200, v5.x);
    float _214 = v5.y - 0.0f;
    float2 _217 = float2(_210, _211);
    float4 _219 = t1.Sample(s1, _217);
    float2 _248 = float2(_213, _214);
    float4 _250 = t1.Sample(s1, _248);
    float _272 = mad(asfloat((asuint(t2.Sample(s2, _217).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _248).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float4 _290 = t1.Sample(s1, _122);
    float3 _310 = float3(mad(asfloat((asuint(_290.x) & cb3_m[46u].x) | cb3_m[47u].x), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].y | (asuint(_290.y) & cb3_m[46u].y)), 2.0f, -1.0f), mad(asfloat(cb3_m[47u].z | (asuint(_290.z) & cb3_m[46u].z)), 2.0f, -1.0f));
    float _326 = (((!(max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_219.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_250.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_219.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_250.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_219.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_250.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f)) || ((((!(mad(-_141, _272, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _272, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f)) || ((-_181) >= 0.0f)) ? 1.0f : mad(-_145, _191, 1.0f);
    float _331 = asfloat(cb4_m[210u].y);
    float _335 = (abs(_331) > 0.0f) ? (1.0f / _331) : 9.999999933815812510711506376258e+36f;
    float _336 = _207 * _335;
    float _337 = v5.x + 0.0f;
    float _338 = mad(_207, _335, v5.y);
    float _339 = v5.x - 0.0f;
    float _341 = mad(-_207, _335, v5.y);
    float2 _342 = float2(_337, _338);
    float4 _344 = t1.Sample(s1, _342);
    float2 _363 = float2(_339, _341);
    float4 _365 = t1.Sample(s1, _363);
    float _387 = mad(asfloat((asuint(t2.Sample(s2, _342).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _363).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _419 = ((1.0f - _181) >= 0.0f) ? _326 : (((((!(mad(-_141, _387, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _387, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _326 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_344.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_365.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat(cb3_m[47u].y | (asuint(_344.y) & cb3_m[46u].y)) + asfloat((asuint(_365.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat(cb3_m[47u].z | (asuint(_344.z) & cb3_m[46u].z)) + asfloat(cb3_m[47u].z | (asuint(_365.z) & cb3_m[46u].z))) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _326) : _326));
    float2 _422 = float2(_213, _341);
    float4 _424 = t1.Sample(s1, _422);
    float2 _443 = float2(_210, _338);
    float4 _445 = t1.Sample(s1, _443);
    float _467 = mad(asfloat((asuint(t2.Sample(s2, _422).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _443).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _499 = ((2.0f - _181) >= 0.0f) ? _419 : (((((!(mad(-_141, _467, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _467, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _419 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_424.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_445.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_424.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_445.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat(cb3_m[47u].z | (asuint(_424.z) & cb3_m[46u].z)) + asfloat(cb3_m[47u].z | (asuint(_445.z) & cb3_m[46u].z))) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _419) : _419));
    float _502 = _207 * 1.0f;
    float2 _507 = float2(_213, mad(_335, _502, v5.y));
    float4 _509 = t1.Sample(s1, _507);
    float2 _528 = float2(mad(_209, 1.0f, v5.x), mad(_335 * _502, -1.0f, v5.y));
    float4 _530 = t1.Sample(s1, _528);
    float _552 = mad(asfloat((asuint(t2.Sample(s2, _507).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _528).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _584 = ((3.0f - _181) >= 0.0f) ? _499 : (((((!(mad(-_141, _552, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _552, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _499 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_509.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_530.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_509.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_530.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat(cb3_m[47u].z | (asuint(_509.z) & cb3_m[46u].z)) + asfloat(cb3_m[47u].z | (asuint(_530.z) & cb3_m[46u].z))) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _499) : _499));
    float _587 = _207 + _207;
    float2 _594 = float2(mad(_587, _200, v5.x), _211);
    float4 _596 = t1.Sample(s1, _594);
    float2 _609 = float2(mad(-_587, _200, v5.x), _214);
    float4 _611 = t1.Sample(s1, _609);
    float _658 = mad(asfloat((asuint(t2.Sample(s2, _594).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _609).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _671 = ((4.0f - _181) >= 0.0f) ? _584 : (((((mad(-_141, _658, 0.001000000047497451305389404296875f) >= 0.0f) || (!(mad(-_141, _658, 0.100000001490116119384765625f) >= 0.0f))) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _584 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_596.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_611.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_596.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_611.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat(cb3_m[47u].z | (asuint(_596.z) & cb3_m[46u].z)) + asfloat(cb3_m[47u].z | (asuint(_611.z) & cb3_m[46u].z))) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _584) : _584));
    float2 _674 = float2(_337, mad(_587, _335, v5.y));
    float4 _676 = t1.Sample(s1, _674);
    float2 _695 = float2(_339, mad(-_587, _335, v5.y));
    float4 _697 = t1.Sample(s1, _695);
    float _719 = mad(asfloat((asuint(t2.Sample(s2, _695).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _674).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _755 = ((5.0f - _181) >= 0.0f) ? _671 : (((((!(mad(-_141, _719, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _719, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _671 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_676.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_697.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_676.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_697.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat(cb3_m[47u].z | (asuint(_676.z) & cb3_m[46u].z)) + asfloat((asuint(_697.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _671) : _671));
    float _758 = _207 * (-2.0f);
    float _759 = _207 * 3.0f;
    float _760 = _207 * (-3.0f);
    float _761 = _207 * 4.0f;
    float _762 = _758 * _200;
    float _763 = _759 * _200;
    float _764 = _760 * _200;
    float _765 = _761 * _200;
    float _766 = _759 * _335;
    float _767 = _761 * _335;
    float _771 = mad(_762, 0.0f, v5.y);
    float _779 = mad(-_762, 0.0f, v5.y);
    float2 _780 = float2(mad(_762, 1.0f, v5.x), mad(_758, _335, v5.y));
    float4 _782 = t1.Sample(s1, _780);
    float2 _795 = float2(mad(-_762, 1.0f, v5.x), mad(-_758, _335, v5.y));
    float4 _797 = t1.Sample(s1, _795);
    float _844 = mad(asfloat((asuint(t2.Sample(s2, _780).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _795).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _857 = ((6.0f - _181) >= 0.0f) ? _755 : (((((!(mad(-_141, _844, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _844, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _755 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_782.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_797.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_782.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_797.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_782.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_797.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _755) : _755));
    float2 _864 = float2(mad(_209, -2.0f, v5.x), mad(_336, 2.0f, v5.y));
    float4 _866 = t1.Sample(s1, _864);
    float2 _879 = float2(mad(_209, 2.0f, v5.x), mad(_336, -2.0f, v5.y));
    float4 _881 = t1.Sample(s1, _879);
    float _928 = mad(asfloat((asuint(t2.Sample(s2, _879).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _864).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _941 = ((7.0f - _181) >= 0.0f) ? _857 : (((((mad(-_141, _928, 0.001000000047497451305389404296875f) >= 0.0f) || (!(mad(-_141, _928, 0.100000001490116119384765625f) >= 0.0f))) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _857 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_866.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_881.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_866.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_881.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_866.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_881.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _857) : _857));
    float2 _944 = float2(mad(_763, 1.0f, v5.x), _771);
    float4 _946 = t1.Sample(s1, _944);
    float2 _965 = float2(mad(-_763, 1.0f, v5.x), _779);
    float4 _967 = t1.Sample(s1, _965);
    float _989 = mad(asfloat((asuint(t2.Sample(s2, _965).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _944).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1021 = ((8.0f - _181) >= 0.0f) ? _941 : (((((!(mad(-_141, _989, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _989, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _941 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_946.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_967.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_946.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_967.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_946.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_967.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _941) : _941));
    float _1024 = mad(_766, 0.0f, v5.x);
    float2 _1027 = float2(_1024, mad(_766, 1.0f, v5.y));
    float4 _1029 = t1.Sample(s1, _1027);
    float _1042 = mad(_766, -0.0f, v5.x);
    float2 _1045 = float2(_1042, mad(_766, -1.0f, v5.y));
    float4 _1047 = t1.Sample(s1, _1045);
    float _1094 = mad(asfloat((asuint(t2.Sample(s2, _1027).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1045).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1114 = ((9.0f - _181) >= 0.0f) ? _1021 : (((((!(mad(-_141, _1094, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1094, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1021 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1047.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1029.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1047.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1029.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1047.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1029.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1021) : _1021));
    float2 _1126 = float2(mad(_764, 1.0f, v5.x), mad(_760, _335, v5.y));
    float4 _1128 = t1.Sample(s1, _1126);
    float2 _1141 = float2(mad(-_764, 1.0f, v5.x), mad(-_760, _335, v5.y));
    float4 _1143 = t1.Sample(s1, _1141);
    float _1190 = mad(asfloat((asuint(t2.Sample(s2, _1126).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1141).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1203 = ((10.0f - _181) >= 0.0f) ? _1114 : (((((!(mad(-_141, _1190, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1190, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1114 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1128.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1143.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1128.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1143.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1128.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1143.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1114) : _1114));
    float2 _1214 = float2(mad(_209, -3.0f, v5.x), mad(_336, 3.0f, v5.y));
    float4 _1216 = t1.Sample(s1, _1214);
    float2 _1229 = float2(mad(_209, 3.0f, v5.x), mad(_336, -3.0f, v5.y));
    float4 _1231 = t1.Sample(s1, _1229);
    float _1278 = mad(asfloat((asuint(t2.Sample(s2, _1229).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1214).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1291 = ((11.0f - _181) >= 0.0f) ? _1203 : (((((mad(-_141, _1278, 0.001000000047497451305389404296875f) >= 0.0f) || (!(mad(-_141, _1278, 0.100000001490116119384765625f) >= 0.0f))) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1203 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1216.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1231.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1216.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1231.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1216.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1231.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1203) : _1203));
    float2 _1294 = float2(mad(_765, 1.0f, v5.x), _771);
    float4 _1296 = t1.Sample(s1, _1294);
    float2 _1315 = float2(mad(-_765, 1.0f, v5.x), _779);
    float4 _1317 = t1.Sample(s1, _1315);
    float _1339 = mad(asfloat((asuint(t2.Sample(s2, _1315).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1294).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1371 = ((12.0f - _181) >= 0.0f) ? _1291 : (((((!(mad(-_141, _1339, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1339, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1291 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1296.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1317.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1296.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1317.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1296.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1317.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1291) : _1291));
    float2 _1374 = float2(_1024, mad(_767, 1.0f, v5.y));
    float4 _1376 = t1.Sample(s1, _1374);
    float2 _1395 = float2(_1042, mad(_767, -1.0f, v5.y));
    float4 _1397 = t1.Sample(s1, _1395);
    float _1419 = mad(asfloat((asuint(t2.Sample(s2, _1374).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1395).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1451 = ((13.0f - _181) >= 0.0f) ? _1371 : (((((!(mad(-_141, _1419, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1419, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1371 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1376.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1397.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1376.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1397.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1376.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1397.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1371) : _1371));
    float _1454 = _207 * (-4.0f);
    float2 _1461 = float2(mad(_1454, _200, v5.x), mad(_1454, _335, v5.y));
    float4 _1463 = t1.Sample(s1, _1461);
    float2 _1482 = float2(mad(-_1454, _200, v5.x), mad(-_1454, _335, v5.y));
    float4 _1484 = t1.Sample(s1, _1482);
    float _1506 = mad(asfloat((asuint(t2.Sample(s2, _1461).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1482).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1538 = ((14.0f - _181) >= 0.0f) ? _1451 : (((((!(mad(-_141, _1506, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1506, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1451 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1463.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1484.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((asuint(_1463.y) & cb3_m[46u].y) | cb3_m[47u].y) + asfloat((asuint(_1484.y) & cb3_m[46u].y) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1463.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1484.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1451) : _1451));
    float2 _1541 = float2(mad(_209, -4.0f, v5.x), mad(_336, 4.0f, v5.y));
    float4 _1543 = t1.Sample(s1, _1541);
    float2 _1556 = float2(mad(_209, 4.0f, v5.x), mad(_336, -4.0f, v5.y));
    float4 _1558 = t1.Sample(s1, _1556);
    float _1605 = mad(asfloat((asuint(t2.Sample(s2, _1556).x) & cb3_m[48u].x) | cb3_m[49u].x) + asfloat((asuint(t2.Sample(s2, _1541).x) & cb3_m[48u].x) | cb3_m[49u].x), -0.5f, _138);
    float _1621 = (_145 * asfloat(cb4_m[214u].z)) * log2(abs(((15.0f - _181) >= 0.0f) ? _1538 : (((((!(mad(-_141, _1605, 0.100000001490116119384765625f) >= 0.0f)) || (mad(-_141, _1605, 0.001000000047497451305389404296875f) >= 0.0f)) ? (-0.0f) : (-1.0f)) >= 0.0f) ? _1538 : ((max(0.699999988079071044921875f - abs(dp3_f32(float3((asfloat((asuint(_1543.x) & cb3_m[46u].x) | cb3_m[47u].x) + asfloat((asuint(_1558.x) & cb3_m[46u].x) | cb3_m[47u].x)) - 1.0f, (asfloat((cb3_m[46u].y & asuint(_1543.y)) | cb3_m[47u].y) + asfloat((cb3_m[46u].y & asuint(_1558.y)) | cb3_m[47u].y)) - 1.0f, (asfloat((asuint(_1543.z) & cb3_m[46u].z) | cb3_m[47u].z) + asfloat((asuint(_1558.z) & cb3_m[46u].z) | cb3_m[47u].z)) - 1.0f), _310)), -0.300000011920928955078125f) >= 0.0f) ? mad(-_145, _191, _1538) : _1538))));
    float _1625 = clamp(exp2(isnan(_1621) ? 0.0f : _1621), 0.0f, 1.0f);
    float4 _1629 = t0.Sample(s0, _122);
    o0.x = _1625 * asfloat(cb3_m[45u].x | (cb3_m[44u].x & asuint(_1629.x)));
    o0.y = _1625 * asfloat(cb3_m[45u].y | (cb3_m[44u].y & asuint(_1629.y)));
    o0.z = _1625 * asfloat(cb3_m[45u].z | (cb3_m[44u].z & asuint(_1629.z)));
    o0.w = asfloat(cb3_m[45u].w | (cb3_m[44u].w & asuint(_1629.w)));
}