#define FXAA_HLSL_5 1
#if 1 // Optional: force max quality (otherwise it falls back on the default)
#define FXAA_QUALITY__PRESET 39
#endif

#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/FXAA.hlsl"

cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m[46] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m[193] : packoffset(c0);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

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
#if 1 // Luma: enable improved FXAA

  FxaaTex tex;
  tex.tex = t0; // We store the perceptually encoded luminance in the alpha channel!
  tex.smpl = s0;
  FxaaFloat2 fxaaQualityRcpFrame = LumaSettings.SwapchainInvSize;
  FxaaFloat fxaaQualitySubpix = FxaaFloat(0.75);
#if FXAA_QUALITY__PRESET >= 39
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.125); // Increase default quality
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0312); // Increase default quality
#else
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.166);
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0833);
#endif

  // The 0 params are console exclusive
  o0 = FxaaPixelShader(
    v5.xy,
    0.0,
    tex,
    tex,
    tex,
    fxaaQualityRcpFrame,
    0.0,
    0.0,
    0.0,
    fxaaQualitySubpix,
    fxaaQualityEdgeThreshold,
    fxaaQualityEdgeThresholdMin).xyzw;

#else

    float _66 = mad(v5.z, 0.0f, v5.x);
    float _71 = _66 * 1.0f;
    float _72 = mad(v5.w, -1.0f, v5.y) * 1.0f;
    float _73 = _66 * 0.0f;
    float4 _80 = t0.SampleLevel(s0, float2(_71, _72), _73);
    float _105 = mad(v5.z, -1.0f, v5.x) * 1.0f;
    float _106 = mad(v5.w, 0.0f, v5.y) * 1.0f;
    float4 _109 = t0.SampleLevel(s0, float2(_105, _106), _73);
    float _124 = v5.x * 0.0f;
    float4 _127 = t0.SampleLevel(s0, float2(v5.x * 1.0f, v5.y * 1.0f), _124);
    uint _137 = (asuint(_127.x) & cb3_m[44u].x) | cb3_m[45u].x;
    uint _138 = (asuint(_127.y) & cb3_m[44u].y) | cb3_m[45u].y;
    uint _139 = (asuint(_127.z) & cb3_m[44u].z) | cb3_m[45u].z;
    float _142 = mad(v5.z, 1.0f, v5.x);
    float _144 = _142 * 1.0f;
    float _145 = _142 * 0.0f;
    float4 _148 = t0.SampleLevel(s0, float2(_144, _106), _145);
    float _161 = mad(v5.w, 1.0f, v5.y) * 1.0f;
    float4 _164 = t0.SampleLevel(s0, float2(_71, _161), _145);
    float _177 = asfloat((asuint(_80.y) & cb3_m[44u].y) | cb3_m[45u].y);
    float _178 = asfloat((asuint(_80.x) & cb3_m[44u].x) | cb3_m[45u].x);
    float _179 = mad(_177, 1.96321070194244384765625f, _178);
    float _180 = asfloat((asuint(_109.y) & cb3_m[44u].y) | cb3_m[45u].y);
    float _181 = asfloat((asuint(_109.x) & cb3_m[44u].x) | cb3_m[45u].x);
    float _182 = mad(_180, 1.96321070194244384765625f, _181);
    float _183 = asfloat(_138);
    float _184 = asfloat(_137);
    float _185 = mad(_183, 1.96321070194244384765625f, _184);
    float _186 = asfloat((asuint(_148.y) & cb3_m[44u].y) | cb3_m[45u].y);
    float _187 = asfloat((asuint(_148.x) & cb3_m[44u].x) | cb3_m[45u].x);
    float _188 = mad(_186, 1.96321070194244384765625f, _187);
    float _189 = asfloat((asuint(_164.y) & cb3_m[44u].y) | cb3_m[45u].y);
    float _190 = asfloat((asuint(_164.x) & cb3_m[44u].x) | cb3_m[45u].x);
    float _191 = mad(_189, 1.96321070194244384765625f, _190);
    float _199 = max(_185, max(max(_179, _182), max(_188, _191)));
    float _200 = _199 - min(_185, min(min(_179, _182), min(_188, _191)));
    uint _589;
    uint _590;
    uint _591;
    if (_200 < (-min(-(_199 * 0.125f), -0.0416666679084300994873046875f)))
    {
        _589 = _139;
        _590 = _138;
        _591 = _137;
    }
    else
    {
        float _236 = mad(abs(mad(_191 + (_188 + (_179 + _182)), 0.25f, -_185)), (abs(_200) > 0.0f) ? (1.0f / _200) : 9.999999933815812510711506376258e+36f, -0.25f);
        float _240 = min((_236 >= 0.0f) ? (_236 * 1.33333337306976318359375f) : 0.0f, 0.75f);
        float4 _245 = t0.SampleLevel(s0, float2(v5.x - v5.z, v5.y - v5.w), 0.0f);
        float4 _260 = t0.SampleLevel(s0, float2(_144, _72), _145);
        float4 _275 = t0.SampleLevel(s0, float2(_105, _161), _145);
        float4 _292 = t0.SampleLevel(s0, float2(v5.z + v5.x, v5.w + v5.y), 0.0f);
        float _305 = asfloat((asuint(_245.x) & cb3_m[44u].x) | cb3_m[45u].x);
        float _306 = asfloat((asuint(_245.y) & cb3_m[44u].y) | cb3_m[45u].y);
        float _308 = asfloat((asuint(_260.x) & cb3_m[44u].x) | cb3_m[45u].x);
        float _309 = asfloat((asuint(_260.y) & cb3_m[44u].y) | cb3_m[45u].y);
        float _314 = asfloat((asuint(_275.x) & cb3_m[44u].x) | cb3_m[45u].x);
        float _315 = asfloat((asuint(_275.y) & cb3_m[44u].y) | cb3_m[45u].y);
        float _320 = asfloat((asuint(_292.x) & cb3_m[44u].x) | cb3_m[45u].x);
        float _321 = asfloat((asuint(_292.y) & cb3_m[44u].y) | cb3_m[45u].y);
        float _333 = mad(_309, 1.96321070194244384765625f, _308);
        float _334 = mad(_315, 1.96321070194244384765625f, _314);
        float _335 = mad(_321, 1.96321070194244384765625f, _320);
        float _337 = mad(_306, 1.96321070194244384765625f, _305) * 0.25f;
        bool _368 = (((abs(mad(_191, 0.5f, mad(_179, 0.5f, -_185))) + abs(mad(_334, 0.25f, _337 + (_182 * (-0.5f))))) + abs(mad(_335, 0.25f, (_333 * 0.25f) + (_188 * (-0.5f))))) - ((abs(mad(_333, 0.25f, (_179 * (-0.5f)) + _337)) + abs(mad(_188, 0.5f, mad(_182, 0.5f, -_185)))) + abs(mad(_335, 0.25f, (_191 * (-0.5f)) + (_334 * 0.25f))))) >= 0.0f;
        float _369 = _368 ? v5.w : v5.z;
        float _371 = _368 ? _179 : _182;
        float _372 = _368 ? _191 : _188;
        float _376 = (_185 + _371) * 0.5f;
        float _378 = (_185 + _372) * 0.5f;
        float _379 = abs(_371 - _185);
        float _380 = abs(_372 - _185);
        bool _382 = (_379 - _380) >= 0.0f;
        float _383 = _382 ? _376 : _378;
        uint _386 = _382 ? asuint(_376) : asuint(_378);
        float _387 = max(_379, _380);
        float _388 = _382 ? (-_369) : _369;
        float _389 = _388 * 0.5f;
        float _392 = v5.x + (_368 ? 0.0f : _389);
        float _393 = v5.y + (_368 ? _389 : 0.0f);
        float _394 = _368 ? (v5.z * 1.0f) : _124;
        float _395 = _368 ? _124 : (v5.w * 1.0f);
        uint _405;
        uint _408;
        uint _414;
        uint _416;
        _405 = asuint(_395 + _393);
        _408 = asuint(_392 + _394);
        _414 = asuint(_393 - _395);
        _416 = asuint(_392 - _394);
        uint _406;
        uint _409;
        bool _413;
        uint _415;
        uint _417;
        bool _421;
        int _423;
        uint _411;
        uint _419;
        float _520;
        float _521;
        uint _410 = _386;
        bool _412 = true;
        uint _418 = _386;
        bool _420 = true;
        int _422 = 16;
        float _424;
        float _425;
        float _426;
        float _427;
        for (;;)
        {
            _424 = _420 ? 0.0f : 1.0f;
            _425 = _412 ? 0.0f : 1.0f;
            _426 = asfloat(_410);
            _427 = asfloat(_418);
            if (uint(_422) == 0u)
            {
                _520 = _427;
                _521 = _426;
                break;
            }
            if (_424 != (-_424))
            {
                _419 = _418;
            }
            else
            {
                float _439 = asfloat(_416);
                float4 _446 = t0.SampleLevel(s0, float2(_439 * 1.0f, asfloat(_414) * 1.0f), _439 * 0.0f);
                _419 = asuint(mad(asfloat((asuint(_446.y) & cb3_m[44u].y) | cb3_m[45u].y), 1.96321070194244384765625f, asfloat((asuint(_446.x) & cb3_m[44u].x) | cb3_m[45u].x)));
            }
            float _459 = asfloat(_419);
            if ((-_425) != _425)
            {
                _411 = _410;
            }
            else
            {
                float _465 = asfloat(_408);
                float4 _472 = t0.SampleLevel(s0, float2(_465 * 1.0f, asfloat(_405) * 1.0f), _465 * 0.0f);
                _411 = asuint(mad(asfloat((asuint(_472.y) & cb3_m[44u].y) | cb3_m[45u].y), 1.96321070194244384765625f, asfloat((asuint(_472.x) & cb3_m[44u].x) | cb3_m[45u].x)));
            }
            float _485 = asfloat(_411);
            _421 = (-(_424 + float(mad(_387, -0.25f, abs(_459 - _383)) >= 0.0f))) >= 0.0f;
            _413 = (-(float(mad(_387, -0.25f, abs(_485 - _383)) >= 0.0f) + _425)) >= 0.0f;
            float _502 = (_413 ? 0.0f : 1.0f) * (_421 ? 0.0f : 1.0f);
            if (_502 != (-_502))
            {
                _520 = _459;
                _521 = _485;
                break;
            }
            _417 = _421 ? asuint(asfloat(_416) - _394) : _416;
            _415 = _421 ? asuint(asfloat(_414) - _395) : _414;
            _409 = _413 ? asuint(asfloat(_408) + _394) : _408;
            _406 = _413 ? asuint(asfloat(_405) + _395) : _405;
            _423 = _422 - 1;
            _405 = _406;
            _408 = _409;
            _410 = _411;
            _412 = _413;
            _414 = _415;
            _416 = _417;
            _418 = _419;
            _420 = _421;
            _422 = _423;
            continue;
        }
        float _526 = _368 ? (v5.x - asfloat(_416)) : (v5.y - asfloat(_414));
        float _531 = _368 ? (asfloat(_408) - v5.x) : (asfloat(_405) - v5.y);
        float _546 = _526 + _531;
        float _554 = mad(-((abs(_546) > 0.0f) ? (1.0f / _546) : 9.999999933815812510711506376258e+36f), min(_526, _531), 0.5f) * (((-abs((((_185 - _383) >= 0.0f) ? 0.0f : 1.0f) + ((((((_526 - _531) >= 0.0f) ? _521 : _520) - _383) >= 0.0f) ? (-0.0f) : (-1.0f)))) >= 0.0f) ? 0.0f : _388);
        float4 _561 = t0.SampleLevel(s0, float2(v5.x + (_368 ? 0.0f : _554), v5.y + (_368 ? _554 : 0.0f)), 0.0f);
        float _574 = asfloat((asuint(_561.x) & cb3_m[44u].x) | cb3_m[45u].x);
        float _575 = asfloat((asuint(_561.y) & cb3_m[44u].y) | cb3_m[45u].y);
        float _576 = asfloat((asuint(_561.z) & cb3_m[44u].z) | cb3_m[45u].z);
        float _579 = mad(_240 * ((asfloat((asuint(_164.z) & cb3_m[44u].z) | cb3_m[45u].z) + (asfloat((asuint(_148.z) & cb3_m[44u].z) | cb3_m[45u].z) + (asfloat(_139) + (asfloat((asuint(_109.z) & cb3_m[44u].z) | cb3_m[45u].z) + asfloat((asuint(_80.z) & cb3_m[44u].z) | cb3_m[45u].z))))) + (asfloat(cb3_m[45u].z | (asuint(_292.z) & cb3_m[44u].z)) + (asfloat((asuint(_275.z) & cb3_m[44u].z) | cb3_m[45u].z) + (asfloat((asuint(_245.z) & cb3_m[44u].z) | cb3_m[45u].z) + asfloat((asuint(_260.z) & cb3_m[44u].z) | cb3_m[45u].z))))), 0.111111111938953399658203125f, _576);
        _589 = asuint(mad(-_240, _576, _579));
        _590 = asuint(mad(-_240, _575, mad(_240 * ((_189 + (_186 + (_183 + (_177 + _180)))) + (_321 + (_315 + (_306 + _309)))), 0.111111111938953399658203125f, _575)));
        _591 = asuint(mad(-_240, _574, mad(_240 * ((_190 + (_187 + ((_178 + _181) + _184))) + (((_305 + _308) + _314) + _320)), 0.111111111938953399658203125f, _574)));
    }
    o0.x = asfloat(_591) * asfloat(cb4_m[192u].x);
    o0.y = asfloat(_590) * asfloat(cb4_m[192u].y);
    o0.z = asfloat(_589) * asfloat(cb4_m[192u].z);

#endif

    o0.w = asfloat(cb4_m[192u].w);
}