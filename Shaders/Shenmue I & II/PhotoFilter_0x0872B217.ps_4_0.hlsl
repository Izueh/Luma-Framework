#include "../Includes/Common.hlsl"

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
    uint4 cb0_m[81] : packoffset(c0);
};
#endif

SamplerState s0 : register(s1);
SamplerState s1 : register(s2);
SamplerState s2 : register(s3);
SamplerState s3 : register(s4);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);

float dp3_f32(precise float3 a, precise float3 b)
{
    precise float _101 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _101));
}

float dp4_f32(precise float4 a, precise float4 b)
{
    precise float _83 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _83)));
}

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
    precise float4 _121 = t2.Sample(s2, float2(v3.x, v3.y));
    precise float _122 = _121.x;
    precise float _123 = _121.y;
    precise float _124 = _121.z;
    precise float _129 = asfloat(cb0_m[46u].x);
    precise float _132 = asfloat(cb0_m[46u].y);
    precise float _133 = _122 * _129;
    precise float _134 = _123 * _129;
    precise float _135 = _124 * _129;
    precise float _136 = _132 + _133;
    precise float _137 = _132 + _134;
    precise float _138 = _132 + _135;
    precise float _141 = asfloat(cb0_m[48u].x);
    precise float _144 = asfloat(cb0_m[48u].y);
    precise float _145 = _122 * _141;
    precise float _146 = _123 * _141;
    precise float _147 = _124 * _141;
    precise float _148 = _145 + _144;
    precise float _149 = _146 + _144;
    precise float _150 = _147 + _144;
    float _152 = dp3_f32(float3(_136, _137, _138), 0.3333333432674407958984375f.xxx);
    precise float _153 = _136 - _152;
    precise float _154 = _137 - _152;
    precise float _155 = _138 - _152;
    precise float _158 = asfloat(cb0_m[47u].x);
    precise float _159 = _153 * _158;
    precise float _160 = _158 * _154;
    precise float _161 = _158 * _155;
    precise float _162 = _152 + _159;
    precise float _163 = _152 + _160;
    precise float _164 = _152 + _161;
    precise float _170 = asfloat(cb0_m[46u].z);
    precise float _173 = asfloat(cb0_m[46u].w);
    precise float _174 = saturate(_162) * _170;
    precise float _175 = _170 * saturate(_163);
    precise float _176 = _170 * saturate(_164);
    precise float _177 = _174 + _173;
    precise float _178 = _173 + _175;
    precise float _179 = _173 + _176;
    float _181 = dp3_f32(float3(_148, _149, _150), 0.3333333432674407958984375f.xxx);
    precise float _182 = _148 - _181;
    precise float _183 = _149 - _181;
    precise float _184 = _150 - _181;
    precise float _187 = asfloat(cb0_m[49u].x);
    precise float _188 = _182 * _187;
    precise float _189 = _187 * _183;
    precise float _190 = _187 * _184;
    precise float _191 = _181 + _188;
    precise float _192 = _181 + _189;
    precise float _193 = _181 + _190;
    precise float _196 = asfloat(cb0_m[48u].z);
    precise float _199 = asfloat(cb0_m[48u].w);
    precise float _200 = _191 * _196;
    precise float _201 = _196 * _192;
    precise float _202 = _196 * _193;
    precise float _203 = _200 + _199;
    precise float _204 = _199 + _201;
    precise float _205 = _199 + _202;
    precise float4 _214 = t1.Sample(s1, float2(v1.x, v1.y));
    precise float _218 = _203 * _214.x;
    precise float _219 = _214.y * _204;
    precise float _220 = _214.z * _205;
    precise float _221 = _177 + _218;
    precise float _222 = _219 + _178;
    precise float _223 = _220 + _179;
    precise float _226 = asfloat(cb0_m[14u].y);
    precise float _227 = _221 * _226;
    precise float _228 = _226 * _222;
    precise float _229 = _226 * _223;
    precise float4 _241 = t0.Sample(s0, float2(v0.x, v0.y));
    precise float _242 = _241.x;
    precise float _243 = _241.y;
    precise float _244 = _241.z;
    precise float _247 = asfloat(cb0_m[4u].x);
    precise float _248 = _242 + _247;
    precise float _249 = _243 + _247;
    precise float _250 = _244 + _247;
    precise float _251 = _248 * _242;
    precise float _252 = _243 * _249;
    precise float _253 = _244 * _250;
    precise float _254 = _251 + 1.0f;
    precise float _255 = _252 + 1.0f;
    precise float _256 = _253 + 1.0f;
    precise float _260 = _242 + sqrt(_254);
    precise float _261 = _243 + sqrt(_255);
    precise float _262 = _244 + sqrt(_256);
    precise float _279 = dp4_f32(_241, float4(asfloat(cb0_m[15u].x), asfloat(cb0_m[15u].y), asfloat(cb0_m[15u].z), asfloat(cb0_m[15u].w))) + asfloat(cb0_m[14u].w);
    o0.w = saturate(_279);
    precise float _283 = _260 - 1.0f;
    precise float _284 = _261 - 1.0f;
    precise float _285 = _262 - 1.0f;
    precise float _288 = asfloat(cb0_m[4u].w);
    precise float _289 = _288 * _283;
    precise float _290 = _288 * _284;
    precise float _291 = _288 * _285;
    precise float _292 = _203 * _289;
    precise float _293 = _290 * _204;
    precise float _294 = _291 * _205;
    precise float _297 = asfloat(cb0_m[12u].y);
    precise float _298 = _292 * _297;
    precise float _299 = _297 * _293;
    precise float _300 = _297 * _294;
    precise float _301 = _298 + 1.0f;
    precise float _302 = _299 + 1.0f;
    precise float _303 = _300 + 1.0f;
    precise float _306 = asfloat(cb0_m[12u].z);
    precise float _307 = _298 * _306;
    precise float _308 = _306 * _299;
    precise float _309 = _306 * _300;
    precise float _310 = _307 + 1.0f;
    precise float _311 = _308 + 1.0f;
    precise float _312 = _309 + 1.0f;
    precise float _313 = _298 * _310;
    precise float _314 = _311 * _299;
    precise float _315 = _312 * _300;
    precise float _316 = _313 / _301;
    precise float _317 = _314 / _302;
    precise float _318 = _315 / _303;
    precise float _319 = saturate(_316);
    precise float _320 = saturate(_317);
    precise float _321 = saturate(_318);
    precise float _322 = 1.0f - _319;
    precise float _323 = 1.0f - _320;
    precise float _324 = 1.0f - _321;
    precise float _325 = saturate(_227) * _322;
    precise float _326 = _323 * saturate(_228);
    precise float _327 = _324 * saturate(_229);
    precise float _328 = _319 + _325;
    precise float _329 = _326 + _320;
    precise float _330 = _327 + _321;
    precise float4 _331 = float4(_328, _329, _330, 1.0f);
    precise float _376 = asfloat(cb0_m[10u].x);
    precise float _377 = log2(max(dp4_f32(_331, float4(asfloat(cb0_m[78u].x), asfloat(cb0_m[78u].y), asfloat(cb0_m[78u].z), asfloat(cb0_m[78u].w))), 5.9604644775390625e-08f)) * _376;
    precise float _378 = log2(max(dp4_f32(_331, float4(asfloat(cb0_m[79u].x), asfloat(cb0_m[79u].y), asfloat(cb0_m[79u].z), asfloat(cb0_m[79u].w))), 5.9604644775390625e-08f)) * _376;
    precise float _379 = log2(max(dp4_f32(_331, float4(asfloat(cb0_m[80u].x), asfloat(cb0_m[80u].y), asfloat(cb0_m[80u].z), asfloat(cb0_m[80u].w))), 5.9604644775390625e-08f)) * _376;
    precise float _380 = exp2(_377);
    precise float _381 = exp2(_378);
    precise float _382 = exp2(_379);
    precise float _385 = asfloat(cb0_m[11u].y);
    precise float _386 = _380 * _385;
    precise float _387 = _381 * _385;
    precise float _388 = _382 * _385;
    precise float _402 = t3.Sample(s3, float2(v4.x, v4.y)).x * asfloat(cb0_m[11u].x);
    precise float _403 = _386 + _402;
    precise float _404 = _387 + _402;
    precise float _405 = _388 + _402;
    precise float _408 = asfloat(cb0_m[11u].z);
    precise float _409 = _403 + _408;
    precise float _410 = _404 + _408;
    precise float _411 = _405 + _408;
    precise float _412 = _380 + _409;
    precise float _413 = _410 + _381;
    precise float _414 = _411 + _382;
    o0.x = _412;
    o0.y = _413;
    o0.z = _414;
}