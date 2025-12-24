#include "../Includes/Common.hlsl"

//a // TODO: break this, it's broken!!!
#if 0
cbuffer _Globals : register(b0)
{
  float2 fParam_DepthCastScaleOffset : packoffset(c0) = {1,0};
  float4 fParam_DepthOfFieldFactorScaleOffset : packoffset(c1) = {0.5,0.5,2,-1};
  float4 fParam_HDRFormatFactor_LOGRGB : packoffset(c2);
  float4 fParam_HDRFormatFactor_RGBALUM : packoffset(c3);
  float4 fParam_HDRFormatFactor_REINHARDRGB : packoffset(c4);
  float2 fParam_ScreenSpaceScale : packoffset(c5) = {1,-1};
  float4x4 m44_ModelViewProject : packoffset(c6);
  float4 fParam_GammaCorrection : packoffset(c10) = {0.454545468,0.454545468,0.454545468,0.454545468};
  float4 fParam_DitherOffsetScale : packoffset(c11) = {0.00392156886,0.00392156886,-0.00392156886,0};
  float4 fParam_TonemapMaxMappingLuminance : packoffset(c12) = {1,1,1.015625,1};
  float4 fParam_BrightPass_LensDistortion : packoffset(c13);
  float4 afRGBA_Modulate[32] : packoffset(c14);
  float4 afRGBA_Offset[16] : packoffset(c46);
  float4 afUV_TexCoordOffsetV16[16] : packoffset(c62);
  float4x4 m44_ColorTransformMatrix : packoffset(c78);
  float4x4 m44_PreTonemapColorTransformMatrix : packoffset(c82);
  float4x4 m44_PreTonemapGlareColorTransformMatrix : packoffset(c86);
  float4 fParam_VignetteSimulate : packoffset(c90);
  float fParam_VignettePowerOfCosine : packoffset(c91);
  float4 afUVWQ_TexCoordScaleOffset[4] : packoffset(c92);
  float4 fParam_PerspectiveFactor : packoffset(c96);
  float fParam_FocusDistance : packoffset(c97);
  float4 fParam_DepthOfFieldConvertDepthFactor : packoffset(c98);
  float2 afXY_DepthOfFieldLevelBlendFactor16[16] : packoffset(c99);
  float fParam_DepthOfFieldLayerMaskThreshold : packoffset(c114.z) = {0.25};
  float fParam_DepthOfFieldFactorThreshold : packoffset(c114.w) = {0.00039999999};
  float4 afParam_TexCoordScaler8[8] : packoffset(c115);
  float4 fRGBA_Constant : packoffset(c123);
  float4 afRGBA_Constant[4] : packoffset(c124);
  float4x4 am44_TransformMatrix[8] : packoffset(c128);
  float4 afUV_TexCoordOffsetP32[96] : packoffset(c160);
}
#else
cbuffer cb0_buf : register(b0)
{
    float4 cb0_m[161] : packoffset(c0);
};
#endif

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // 2x2 4 channel texture
Texture2D<float4> t2 : register(t2); // 2x2 4 channel texture
Texture2D<float4> t3 : register(t3); // 2x2 4 channel texture

float dp3_f32(precise float3 a, precise float3 b)
{
    precise float _96 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _96));
}

float dp4_f32(precise float4 a, precise float4 b)
{
    precise float _78 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _78)));
}

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
#if 1
    float4 r0,r1,r2,r3;
    r0.xyzw = t2.Sample(s2, v3.xy).xyzw;
    r1.xyz = mad(r0.xyz, cb0_m[46].x, cb0_m[46].y);
    r0.xyz = mad(r0.xyz, cb0_m[48].x, cb0_m[48].y);
    r0.w = dot(r1.xyz, float3(0.333333, 0.333333, 0.333333));
    r1.xyz = r1.xyz - r0.w;
    r1.xyz = (mad(cb0_m[47].x, r1.xyz, r0.w)); // Luma: removed saturate()
    r1.xyz = mad(r1.xyz, cb0_m[46].z, cb0_m[46].w);
    r0.w = dot(r0.xyz, float3(0.333333, 0.333333, 0.333333));
    r0.xyz = r0.xyz - r0.w;
    r0.xyz = mad(cb0_m[49].x, r0.xyz, r0.w);
    r0.xyz = mad(r0.xyz, cb0_m[48].z, cb0_m[48].w);
    r2.xyzw = t1.Sample(s1, v1.xy, int2(0, 0)).xyzw;
    r1.xyz = mad(r2.xyz, r0.xyz, r1.xyz);
    r1.xyz = (r1.xyz * cb0_m[14].y); // Luma: removed saturate()
    //r1.xyz = min(r1.xyz, 3); // TODO: luma added this to avoid nans but we can do better
    r2.xyzw = t0.Sample(s0, v0.xy, int2(0, 0)).xyzw;
    r3.xyz = r2.xyz + cb0_m[4].x;
    r3.xyz = mad(r3.xyz, r2.xyz, 1.0);
    r3.xyz = sqrt_mirrored(r3.xyz); // Luma: protect against nans
    r3.xyz = r2.xyz + r3.xyz;
    r0.w = dot(r2.xyzw, cb0_m[15].xyzw);
    o0.w = saturate(r0.w + cb0_m[14].w);
    r2.xyz = r3.xyz - 1.0;
    r2.xyz = r2.xyz * cb0_m[4].w;
    r2.xyz = r2.xyz * cb0_m[14].x;
    r0.xyz = r0.xyz * r2.xyz;
    r2.xyz = r0.xyz * cb0_m[12].y;
    r0.xyz = mad(r0.xyz, cb0_m[12].y, 1.0);
    r3.xyz = mad(r2.xyz, cb0_m[12].z, 1.0);
    r2.xyz = r2.xyz * r3.xyz;
    r0.xyz = (r2.xyz / r0.xyz); // Luma: removed saturate() (moved it below)
    r2.xyz = 1.0 - r0.xyz;
    r0.xyz = mad(saturate(r2.xyz), r1.xyz, r0.xyz);

#if _97FF0207 // Photo mode filter!
    r0.w = 1.0;
    r1.x = dot(r0.xyzw, cb0_m[78].xyzw);
    r1.y = dot(r0.xyzw, cb0_m[79].xyzw);
    r1.z = dot(r0.xyzw, cb0_m[80].xyzw);
    r0.xyz = r1.xyz;
#endif
    r1.xyz = r0.xyz * cb0_m[11].y;
    r2.xyzw = t3.Sample(s3, v4.xy, int2(0, 0)).xyzw;
    r1.xyz = mad(r2.x, cb0_m[11].x, r1.xyz);
    r1.xyz = r1.xyz + cb0_m[11].z;
    o0.xyz = r0.xyz + r1.xyz;

#if 0 // OG asm for 0x025857A1
    sample r0.xyzw, v3.xyxx, t2.xyzw, s2
    mad r1.xyz, r0.xyzx, cb0[46].xxxx, cb0[46].yyyy
    mad r0.xyz, r0.xyzx, cb0[48].xxxx, cb0[48].yyyy
    dp3 r0.w, r1.xyzx, l(0.333333, 0.333333, 0.333333, 0.000000)
    add r1.xyz, -r0.wwww, r1.xyzx
    mad_sat r1.xyz, cb0[47].xxxx, r1.xyzx, r0.wwww
    mad r1.xyz, r1.xyzx, cb0[46].zzzz, cb0[46].wwww
    dp3 r0.w, r0.xyzx, l(0.333333, 0.333333, 0.333333, 0.000000)
    add r0.xyz, -r0.wwww, r0.xyzx
    mad r0.xyz, cb0[49].xxxx, r0.xyzx, r0.wwww
    mad r0.xyz, r0.xyzx, cb0[48].zzzz, cb0[48].wwww
    sample_aoffimmi(0,0,0) r2.xyzw, v1.xyxx, t1.xyzw, s1
    mad r1.xyz, r2.xyzx, r0.xyzx, r1.xyzx
    mul_sat r1.xyz, r1.xyzx, cb0[14].yyyy
    sample_aoffimmi(0,0,0) r2.xyzw, v0.xyxx, t0.xyzw, s0
    add r3.xyz, r2.xyzx, cb0[4].xxxx
    mad r3.xyz, r3.xyzx, r2.xyzx, l(1.000000, 1.000000, 1.000000, 0.000000)
    sqrt r3.xyz, r3.xyzx
    add r3.xyz, r2.xyzx, r3.xyzx
    dp4 r0.w, r2.xyzw, cb0[15].xyzw
    add_sat o0.w, r0.w, cb0[14].w
    add r2.xyz, r3.xyzx, l(-1.000000, -1.000000, -1.000000, 0.000000)
    mul r2.xyz, r2.xyzx, cb0[4].wwww
    mul r2.xyz, r2.xyzx, cb0[14].xxxx
    mul r0.xyz, r0.xyzx, r2.xyzx
    mul r2.xyz, r0.xyzx, cb0[12].yyyy
    mad r0.xyz, r0.xyzx, cb0[12].yyyy, l(1.000000, 1.000000, 1.000000, 0.000000)
    mad r3.xyz, r2.xyzx, cb0[12].zzzz, l(1.000000, 1.000000, 1.000000, 0.000000)
    mul r2.xyz, r2.xyzx, r3.xyzx
    div_sat r0.xyz, r2.xyzx, r0.xyzx
    add r2.xyz, -r0.xyzx, l(1.000000, 1.000000, 1.000000, 0.000000)
    mad r0.xyz, r2.xyzx, r1.xyzx, r0.xyzx
    mul r1.xyz, r0.xyzx, cb0[11].yyyy
    sample_aoffimmi(0,0,0) r2.xyzw, v4.xyxx, t3.xyzw, s3
    mad r1.xyz, r2.xxxx, cb0[11].xxxx, r1.xyzx
    add r1.xyz, r1.xyzx, cb0[11].zzzz
    add o0.xyz, r0.xyzx, r1.xyzx
#endif
#else
    precise float4 _116 = t2.Sample(s2, float2(v3.x, v3.y));
    precise float _117 = _116.x;
    precise float _118 = _116.y;
    precise float _119 = _116.z;
    precise float _124 = asfloat(cb0_m[46u].x);
    precise float _127 = asfloat(cb0_m[46u].y);
    precise float _128 = _117 * _124;
    precise float _129 = _118 * _124;
    precise float _130 = _119 * _124;
    precise float _131 = _127 + _128;
    precise float _132 = _127 + _129;
    precise float _133 = _127 + _130;
    precise float _136 = asfloat(cb0_m[48u].x);
    precise float _139 = asfloat(cb0_m[48u].y);
    precise float _140 = _117 * _136;
    precise float _141 = _118 * _136;
    precise float _142 = _119 * _136;
    precise float _143 = _140 + _139;
    precise float _144 = _141 + _139;
    precise float _145 = _142 + _139;
    float _147 = dp3_f32(float3(_131, _132, _133), 0.3333333432674407958984375f.xxx);
    precise float _148 = _131 - _147;
    precise float _149 = _132 - _147;
    precise float _150 = _133 - _147;
    precise float _153 = asfloat(cb0_m[47u].x);
    precise float _154 = _148 * _153;
    precise float _155 = _153 * _149;
    precise float _156 = _153 * _150;
    precise float _157 = _147 + _154;
    precise float _158 = _147 + _155;
    precise float _159 = _147 + _156;
    precise float _165 = asfloat(cb0_m[46u].z);
    precise float _168 = asfloat(cb0_m[46u].w);
    precise float _169 = clamp(_157, 0.0f, 1.0f) * _165;
    precise float _170 = _165 * clamp(_158, 0.0f, 1.0f);
    precise float _171 = _165 * clamp(_159, 0.0f, 1.0f);
    precise float _172 = _169 + _168;
    precise float _173 = _168 + _170;
    precise float _174 = _168 + _171;
    float _176 = dp3_f32(float3(_143, _144, _145), 0.3333333432674407958984375f.xxx);
    precise float _177 = _143 - _176;
    precise float _178 = _144 - _176;
    precise float _179 = _145 - _176;
    precise float _182 = asfloat(cb0_m[49u].x);
    precise float _183 = _177 * _182;
    precise float _184 = _182 * _178;
    precise float _185 = _182 * _179;
    precise float _186 = _176 + _183;
    precise float _187 = _176 + _184;
    precise float _188 = _176 + _185;
    precise float _191 = asfloat(cb0_m[48u].z);
    precise float _194 = asfloat(cb0_m[48u].w);
    precise float _195 = _186 * _191;
    precise float _196 = _191 * _187;
    precise float _197 = _191 * _188;
    precise float _198 = _195 + _194;
    precise float _199 = _194 + _196;
    precise float _200 = _194 + _197;
    precise float4 _209 = t1.Sample(s1, float2(v1.x, v1.y));
    precise float _213 = _198 * _209.x;
    precise float _214 = _209.y * _199;
    precise float _215 = _209.z * _200;
    precise float _216 = _172 + _213;
    precise float _217 = _214 + _173;
    precise float _218 = _215 + _174;
    precise float _221 = asfloat(cb0_m[14u].y);
    precise float _222 = _216 * _221;
    precise float _223 = _221 * _217;
    precise float _224 = _221 * _218;
    precise float4 _236 = t0.Sample(s0, float2(v0.x, v0.y));
    precise float _237 = _236.x;
    precise float _238 = _236.y;
    precise float _239 = _236.z;
    precise float _242 = asfloat(cb0_m[4u].x);
    precise float _243 = _237 + _242;
    precise float _244 = _238 + _242;
    precise float _245 = _239 + _242;
    precise float _246 = _243 * _237;
    precise float _247 = _238 * _244;
    precise float _248 = _239 * _245;
    precise float _249 = _246 + 1.0f;
    precise float _250 = _247 + 1.0f;
    precise float _251 = _248 + 1.0f;
    precise float _255 = _237 + sqrt(_249);
    precise float _256 = _238 + sqrt(_250);
    precise float _257 = _239 + sqrt(_251);
    precise float _274 = dp4_f32(_236, float4(asfloat(cb0_m[15u].x), asfloat(cb0_m[15u].y), asfloat(cb0_m[15u].z), asfloat(cb0_m[15u].w))) + asfloat(cb0_m[14u].w);
    o0.w = clamp(_274, 0.0f, 1.0f);
    precise float _278 = _255 - 1.0f;
    precise float _279 = _256 - 1.0f;
    precise float _280 = _257 - 1.0f;
    precise float _283 = asfloat(cb0_m[4u].w);
    precise float _284 = _283 * _278;
    precise float _285 = _283 * _279;
    precise float _286 = _283 * _280;
    precise float _289 = asfloat(cb0_m[14u].x);
    precise float _290 = _284 * _289;
    precise float _291 = _289 * _285;
    precise float _292 = _289 * _286;
    precise float _293 = _198 * _290;
    precise float _294 = _291 * _199;
    precise float _295 = _292 * _200;
    precise float _298 = asfloat(cb0_m[12u].y);
    precise float _299 = _293 * _298;
    precise float _300 = _298 * _294;
    precise float _301 = _298 * _295;
    precise float _302 = _299 + 1.0f;
    precise float _303 = _300 + 1.0f;
    precise float _304 = _301 + 1.0f;
    precise float _307 = asfloat(cb0_m[12u].z);
    precise float _308 = _299 * _307;
    precise float _309 = _300 * _307;
    precise float _310 = _301 * _307;
    precise float _311 = _308 + 1.0f;
    precise float _312 = _309 + 1.0f;
    precise float _313 = _310 + 1.0f;
    precise float _314 = _299 * _311;
    precise float _315 = _312 * _300;
    precise float _316 = _313 * _301;
    precise float _317 = _314 / _302;
    precise float _318 = _315 / _303;
    precise float _319 = _316 / _304;
    precise float _320 = clamp(_317, 0.0f, 1.0f);
    precise float _321 = clamp(_318, 0.0f, 1.0f);
    precise float _322 = clamp(_319, 0.0f, 1.0f);
    precise float _323 = 1.0f - _320;
    precise float _324 = 1.0f - _321;
    precise float _325 = 1.0f - _322;
    precise float _326 = clamp(_222, 0.0f, 1.0f) * _323;
    precise float _327 = _324 * clamp(_223, 0.0f, 1.0f);
    precise float _328 = _325 * clamp(_224, 0.0f, 1.0f);
    precise float _329 = _320 + _326;
    precise float _330 = _327 + _321;
    precise float _331 = _328 + _322;
    precise float _334 = asfloat(cb0_m[11u].y);
    precise float _335 = _329 * _334;
    precise float _336 = _330 * _334;
    precise float _337 = _331 * _334;
    precise float _351 = t3.Sample(s3, float2(v4.x, v4.y)).x * asfloat(cb0_m[11u].x);
    precise float _352 = _335 + _351;
    precise float _353 = _336 + _351;
    precise float _354 = _337 + _351;
    precise float _357 = asfloat(cb0_m[11u].z);
    precise float _358 = _352 + _357;
    precise float _359 = _353 + _357;
    precise float _360 = _354 + _357;
    precise float _361 = _329 + _358;
    precise float _362 = _359 + _330;
    precise float _363 = _360 + _331;
    o0.x = _361;
    o0.y = _362;
    o0.z = _363;
#endif
}