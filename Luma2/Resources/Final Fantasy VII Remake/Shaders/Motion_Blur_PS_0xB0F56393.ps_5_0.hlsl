#include "Game/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    uint4 cb0_m1 : packoffset(c1);
    uint4 cb0_m2 : packoffset(c2);
    uint4 cb0_m3 : packoffset(c3);
    uint4 cb0_m4 : packoffset(c4);
    uint4 cb0_m5 : packoffset(c5);
    uint4 cb0_m6 : packoffset(c6);
    uint4 cb0_m7 : packoffset(c7);
    uint4 cb0_m8 : packoffset(c8);
    uint4 cb0_m9 : packoffset(c9);
    uint4 cb0_m10 : packoffset(c10);
    uint4 cb0_m11 : packoffset(c11);
    uint4 cb0_m12 : packoffset(c12);
    uint4 cb0_m13 : packoffset(c13);
    uint4 cb0_m14 : packoffset(c14);
    uint4 cb0_m15 : packoffset(c15);
    uint4 cb0_m16 : packoffset(c16);
    uint4 cb0_m17 : packoffset(c17);
    float4 cb0_m18 : packoffset(c18);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m[123] : packoffset(c0);
};

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

SamplerState linearSampler : register(s0);

static float4 gl_FragCoord;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    // Unused, but keeps SV_Position in register 1 as the game's VS (0xB3E3D245) writes it; D3D12 rejects the pipeline otherwise.
    float4 texcoord : TEXCOORD0;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float dp2_f32(float2 a, float2 b)
{
    precise float _68 = a.x * b.x;
    return mad(a.y, b.y, _68);
}

// Helper: sample t1 with linear filtering using pixel-space coords

void frag_main()
{
    bool isUpscaled = (LumaData.GameData.DrewUpscaling != 0);
    float4 resolution = isUpscaled ? LumaData.GameData.OutputResolution : asfloat(cb1_m[122u]);
    float2 hiSize = resolution.xy;                                           // current render target size (t0)
    float2 loSize = float2(asfloat(cb1_m[122].x), asfloat(cb1_m[122].y)); // original t1 size
    float2 hiFragF = floor(gl_FragCoord.xy);
    float2 hiToLo = loSize / hiSize;

    float _93 = floor(gl_FragCoord.x);
    float _94 = floor(gl_FragCoord.y);
    float _101 = frac(frac(dp2_f32(float2(_93, _94), float2(0.067110560834407806396484375f, 0.005837149918079376220703125f))) * 52.98291778564453125f);
    float _104 = _93 + 0.5f;
    float _105 = _94 + 0.5f;
    float _110 = frac(frac(dp2_f32(float2(_93 + 32.66500091552734375f, _94 + 11.81499958038330078125f), float2(0.067110560834407806396484375f, 0.005837149918079376220703125f))) * 52.98291778564453125f);
    float _119 = asfloat(cb1_m[121u].x);
    float _120 = asfloat(cb1_m[121u].y);
    float _133 = asfloat(resolution.x);
    float _134 = asfloat(resolution.y);

    // linear filtered t2 lookup
    // float2 tileCountF = floor((loSize + 15.0f) * (1.0f / 16.0f)); // matches original clamp range
    // float2 pixelCenter = float2(_104 - asfloat(cb1_m[122].x), _105 - asfloat(cb1_m[122].y));
    // float2 jitter = float2(_101, _110) * 0.5f - 0.25f;

    // float2 tileCoordF = pixelCenter * (1.0f / 16.0f) + jitter;
    // tileCoordF = clamp(tileCoordF, 0.0f, tileCountF - 1.0f);
    // float2 uv_t2 = (tileCoordF + 0.5f) / tileCountF;
    // float4 _147 = t2.SampleLevel(linearSampler, uv_t2, 0u);

    // original
    // float4 _147 = t2.Load(int3(uint2(uint(clamp(cvt_f32_i32(floor(mad(_104 - _119, 0.0625f, mad(_101, 0.5f, -0.25f)))), 0, cvt_f32_i32((_133 + 15.0f) * 0.0625f))), uint(clamp(cvt_f32_i32(floor(mad(_105 - _120, 0.0625f, mad(_110, 0.5f, -0.25f)))), 0, cvt_f32_i32((_134 + 15.0f) * 0.0625f)))), 0u));

    // nearest
    float2 tileCountF = floor((loSize + 15.0f) * (1.0f / 16.0f));
    float2 jitter = float2(_101, _110) * 0.5f - 0.25f;

    // Map current high-res pixel center to old resolution space
    float2 pixelCenterLo = (float2(_104, _105) * hiToLo) - float2(_119, _120);
    float2 tileCoordF_lo = floor(pixelCenterLo * (1.0f / 16.0f) + jitter);
    tileCoordF_lo = clamp(tileCoordF_lo, 0.0f, tileCountF - 1.0f);
    uint2 tileCoord_lo = uint2(tileCoordF_lo);
    float4 _147 = t2.Load(int3(tileCoord_lo, 0));
    float _156 = _147.z * cb0_m18.y;
    float _157 = _147.w * cb0_m18.y;
    float2 _158 = float2(_156, _157);
    float _159 = dp2_f32(_158, _158);
    uint2 _163 = uint2(uint(cvt_f32_i32(_93)), uint(cvt_f32_i32(_94)));
    float4 _164 = t0.Load(int3(_163, 0u));
    float _165 = _164.x;
    float _166 = _164.y;
    float _167 = _164.z;
    float _714;
    float _715;
    float _716;
    if (!(_159 < 0.25f))
    {
        float2 _174 = float2(_147.x * cb0_m18.y, _147.y * cb0_m18.y);
        int _180 = cvt_f32_i32(_119);
        int _181 = cvt_f32_i32(_120);
        int _182 = cvt_f32_i32((_119 + _133) - 1.0f);
        int _183 = cvt_f32_i32((_120 + _134) - 1.0f);
        float _711;
        float _712;
        float _713;
        if (dp2_f32(_174, _174) > (_159 * 0.5f))
        {
            float _193 = (1.0f - _101) * 0.2249999940395355224609375f;
            float _194 = (1.0f - _110) * 0.2249999940395355224609375f;
            float _195 = (2.0f - _101) * 0.2249999940395355224609375f;
            float _196 = (2.0f - _110) * 0.2249999940395355224609375f;
            float4 _218 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _193, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_193, _157, _105))), _181, _183))), 0u));
            float4 _225 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _194, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_194, _157, _105))), _181, _183))), 0u));
            float4 _253 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _195, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_195, _157, _105))), _181, _183))), 0u));
            float4 _263 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _196, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_196, _157, _105))), _181, _183))), 0u));
            float _274 = (3.0f - _101) * 0.2249999940395355224609375f;
            float _275 = (3.0f - _110) * 0.2249999940395355224609375f;
            float _276 = (4.0f - _101) * 0.2249999940395355224609375f;
            float _277 = (4.0f - _110) * 0.2249999940395355224609375f;
            float4 _299 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _274, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_274, _157, _105))), _181, _183))), 0u));
            float4 _309 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _275, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_275, _157, _105))), _181, _183))), 0u));
            float4 _337 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _276, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_276, _157, _105))), _181, _183))), 0u));
            float4 _347 = t0.Load(int3(uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _277, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_277, _157, _105))), _181, _183))), 0u));
            _711 = (_347.z + (_337.z + (_309.z + (_299.z + (_263.z + (_253.z + (_218.z + _225.z))))))) * 0.125f;
            _712 = (_347.y + (_337.y + (_309.y + (_299.y + (_263.y + (_253.y + (_218.y + _225.y))))))) * 0.125f;
            _713 = (((((((_218.x + _225.x) + _253.x) + _263.x) + _299.x) + _309.x) + _337.x) + _347.x) * 0.125f;
        }
        else
        {
            float _358 = rsqrt(_159) * 4.0f;

            // uint2 loCoord_center = uint2(hiFragF * hiToLo); // floor
            // float2 uv_center = (float2(loCoord_center) + 0.5f) / loSize;
            // float4 _360 = t1.Sample(linearSampler, uv_center, 0);
            float4 _360 = isUpscaled ? 
                t1.Sample(linearSampler, (float2(_163) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_163, 0));            float _362 = _360.y;
            float _366 = min(_360.x * cb0_m18.y, cb0_m18.w);
            float _371 = (1.0f - _101) * 0.2249999940395355224609375f;
            float _372 = (1.0f - _110) * 0.2249999940395355224609375f;
            float _373 = (2.0f - _101) * 0.2249999940395355224609375f;
            float _374 = (2.0f - _110) * 0.2249999940395355224609375f;

            uint2 _395 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _371, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_371, _157, _105))), _181, _183)));
            float4 _396 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_395) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_395, 0));            // uint2 loCoord_395 = uint2(float2(_395) * hiToLo);
            // float4 _396 = t1.Sample(linearSampler, (float2(loCoord_395) + 0.5f) / loSize, 0);

            float _398 = _396.y;
            
            float4 _399 = t0.Load(int3(_395, 0u));
            float _404 = min(_396.x * cb0_m18.y, cb0_m18.w);
            float _405 = _398 - _362;
            float _412 = clamp(_358 * _366, 0.0f, 1.0f);
            float _416 = dp2_f32(float2(clamp(mad(_405, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_405, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_412, clamp(_358 * _404, 0.0f, 1.0f)));

            uint2 _419 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _372, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_372, _157, _105))), _181, _183)));
            float4 _420 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_419) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_419, 0));            // uint2 loCoord_419 = uint2(float2(_419) * hiToLo);
            // float4 _420 = t1.Sample(linearSampler, (float2(loCoord_419) + 0.5f) / loSize, 0);

            float _422 = _420.y;

            float4 _423 = t0.Load(int3(_419, 0u));
            float _428 = min(_420.x * cb0_m18.y, cb0_m18.w);
            float _429 = _422 - _362;
            float _438 = dp2_f32(float2(clamp(mad(_429, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_429, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_412, clamp(_358 * _428, 0.0f, 1.0f)));
            bool _439 = _398 > _422;
            bool _440 = _404 < _428;
            float _442 = (_439 && _440) ? _438 : _416;
            float _444 = (_439 || _440) ? _438 : _416;

            uint2 _475 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _373, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_373, _157, _105))), _181, _183)));
            float4 _476 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_475) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_475, 0));            // uint2 loCoord_475 = uint2(float2(_475) * hiToLo);
            // float4 _476 = t1.Sample(linearSampler, (float2(loCoord_475) + 0.5f) / loSize, 0);

            float _478 = _476.y;
            
            float4 _479 = t0.Load(int3(_475, 0u));
            float _484 = min(_476.x * cb0_m18.y, cb0_m18.w);
            float _485 = _478 - _362;
            float _490 = _101 - 1.0f;
            float _493 = clamp(mad(_358, _366, _490), 0.0f, 1.0f);
            float _497 = dp2_f32(float2(clamp(mad(_485, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_485, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_493, clamp(mad(_358, _484, _490), 0.0f, 1.0f)));

            uint2 _500 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _374, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_374, _157, _105))), _181, _183)));
            float4 _501 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_500) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_500, 0));            // uint2 loCoord_500 = uint2(float2(_500) * hiToLo);
            // float4 _501 = t1.Sample(linearSampler, (float2(loCoord_500) + 0.5f) / loSize, 0);
            float _503 = _501.y;
            
            float4 _504 = t0.Load(int3(_500, 0u));
            float _509 = min(_501.x * cb0_m18.y, cb0_m18.w);
            float _510 = _503 - _362;
            float _519 = dp2_f32(float2(clamp(mad(_510, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_510, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_493, clamp(mad(_358, _509, _490), 0.0f, 1.0f)));
            bool _520 = _478 > _503;
            bool _521 = _484 < _509;
            float _523 = (_520 && _521) ? _519 : _497;
            float _525 = (_520 || _521) ? _519 : _497;
            float _538 = (3.0f - _101) * 0.2249999940395355224609375f;
            float _539 = (3.0f - _110) * 0.2249999940395355224609375f;
            float _540 = (4.0f - _101) * 0.2249999940395355224609375f;
            float _541 = (4.0f - _110) * 0.2249999940395355224609375f;

            uint2 _562 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _538, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_538, _157, _105))), _181, _183)));
            float4 _563 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_562) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_562, 0));            // uint2 loCoord_562 = uint2(float2(_562) * hiToLo);
            // float4 _563 = t1.Sample(linearSampler, (float2(loCoord_562) + 0.5f) / loSize, 0);
            float _565 = _563.y;

            float4 _566 = t0.Load(int3(_562, 0u));
            float _571 = min(_563.x * cb0_m18.y, cb0_m18.w);
            float _572 = _565 - _362;
            float _577 = _101 - 2.0f;
            float _580 = clamp(mad(_358, _366, _577), 0.0f, 1.0f);
            float _584 = dp2_f32(float2(clamp(mad(_572, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_572, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_580, clamp(mad(_358, _571, _577), 0.0f, 1.0f)));

            uint2 _587 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _539, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_539, _157, _105))), _181, _183)));
            float4 _588 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_587) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_587, 0));            // uint2 loCoord_587 = uint2(float2(_587) * hiToLo);
            // float4 _588 = t1.Sample(linearSampler, (float2(loCoord_587) + 0.5f) / loSize, 0);
            float _590 = _588.y;
            
            float4 _591 = t0.Load(int3(_587, 0u));
            float _596 = min(_588.x * cb0_m18.y, cb0_m18.w);
            float _597 = _590 - _362;
            float _606 = dp2_f32(float2(clamp(mad(_597, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_597, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_580, clamp(mad(_358, _596, _577), 0.0f, 1.0f)));
            bool _607 = _565 > _590;
            bool _608 = _571 < _596;
            float _610 = (_607 && _608) ? _606 : _584;
            float _612 = (_607 || _608) ? _606 : _584;

            uint2 _641 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(_156, _540, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(_540, _157, _105))), _181, _183)));
            float4 _642 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_641) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_641, 0));
            // uint2 loCoord_641 = uint2(float2(_641) * hiToLo);
            // float4 _642 = t1.Sample(linearSampler, (float2(loCoord_641) + 0.5f) / loSize, 0);
            float _644 = _642.y;

            float4 _645 = t0.Load(int3(_641, 0u));
            float _650 = min(_642.x * cb0_m18.y, cb0_m18.w);
            float _651 = _644 - _362;
            float _656 = _101 - 3.0f;
            float _659 = clamp(mad(_358, _366, _656), 0.0f, 1.0f);
            float _663 = dp2_f32(float2(clamp(mad(_651, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_651, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_659, clamp(mad(_358, _650, _656), 0.0f, 1.0f)));

            uint2 _666 = uint2(uint(clamp(_180, cvt_f32_i32(floor(mad(-_156, _541, _104))), _182)), uint(clamp(cvt_f32_i32(floor(mad(-_541, _157, _105))), _181, _183)));
            float4 _667 = isUpscaled ?
                t1.Sample(linearSampler, (float2(_666) + 0.5f) / resolution.xy, 0) :
                t1.Load(int3(_666, 0));
            // uint2 loCoord_666 = uint2(float2(_666) * hiToLo);
            // float4 _667 = t1.Sample(linearSampler, (float2(loCoord_666) + 0.5f) / loSize, 0);
            float _669 = _667.y;
            
            float4 _670 = t0.Load(int3(_666, 0u));
            float _675 = min(_667.x * cb0_m18.y, cb0_m18.w);
            float _676 = _669 - _362;
            float _685 = dp2_f32(float2(clamp(mad(_676, 1.0f, 0.5f), 0.0f, 1.0f), clamp(mad(_676, -1.0f, 0.5f), 0.0f, 1.0f)), float2(_659, clamp(mad(_358, _675, _656), 0.0f, 1.0f)));
            bool _686 = _644 > _669;
            bool _687 = _650 < _675;
            float _689 = (_686 && _687) ? _685 : _663;
            float _691 = (_686 || _687) ? _685 : _663;
            float _701 = max(mad(((((((_444 + _442) + _523) + _525) + _610) + _612) + _689) + _691, -0.125f, 1.0f), 0.0f);
            _711 = (_167 * _701) + (mad(_670.z, _691, mad(_645.z, _689, mad(_591.z, _612, mad(_566.z, _610, mad(_504.z, _525, mad(_479.z, _523, (_399.z * _442) + (_423.z * _444))))))) * 0.125f);
            _712 = (_166 * _701) + (mad(_670.y, _691, mad(_645.y, _689, mad(_591.y, _612, mad(_566.y, _610, mad(_504.y, _525, mad(_479.y, _523, (_399.y * _442) + (_423.y * _444))))))) * 0.125f);
            _713 = (mad(_670.x, _691, mad(_645.x, _689, mad(_591.x, _612, mad(_566.x, _610, mad(_504.x, _525, mad(_479.x, _523, (_423.x * _444) + (_399.x * _442))))))) * 0.125f) + (_165 * _701);
        }
        _714 = _711;
        _715 = _712;
        _716 = _713;
    }
    else
    {
        _714 = _167;
        _715 = _166;
        _716 = _165;
    }
    SV_Target.x = _716;
    SV_Target.y = _715;
    SV_Target.z = _714;
    SV_Target.w = 0.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
