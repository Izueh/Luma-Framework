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
    uint4 cb0_m[50] : packoffset(c0);
};
#endif

// Note: the debug information in the shader didn't actually match the content of its code body // TODO: maybe we shouldn't take anything from it
SamplerState asamp2D_Texture_s : register(s0);
Texture2D<float4> atex2D_Texture : register(t0);
SamplerState s1 : register(s1);
Texture2D<float4> t1 : register(t1);
SamplerState s2 : register(s2);
Texture2D<float4> t2 : register(t2);
SamplerState s3 : register(s3);
Texture2D<float4> t3 : register(t3);

float dp3_f32(precise float3 a, precise float3 b)
{
    return mad(a.z, b.z, mad(a.y, b.y, a.x * b.x));
}

float dp4_f32(precise float4 a, precise float4 b)
{
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, a.x * b.x)));
}

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
    precise float4 _118 = t2.Sample(s2, float2(v3.x, v3.y));
    precise float _119 = _118.x;
    precise float _120 = _118.y;
    precise float _121 = _118.z;
    precise float _126 = asfloat(cb0_m[46u].x);
    precise float _129 = asfloat(cb0_m[46u].y);
    precise float _130 = _119 * _126;
    precise float _131 = _120 * _126;
    precise float _132 = _121 * _126;
    precise float _133 = _129 + _130;
    precise float _134 = _129 + _131;
    precise float _135 = _129 + _132;
    precise float _138 = asfloat(cb0_m[48u].x);
    precise float _141 = asfloat(cb0_m[48u].y);
    precise float _142 = _119 * _138;
    precise float _143 = _120 * _138;
    precise float _144 = _121 * _138;
    precise float _145 = _142 + _141;
    precise float _146 = _143 + _141;
    precise float _147 = _144 + _141;
    float _149 = dp3_f32(float3(_133, _134, _135), 0.3333333432674407958984375f.xxx);
    precise float _150 = _133 - _149;
    precise float _151 = _134 - _149;
    precise float _152 = _135 - _149;
    precise float _155 = asfloat(cb0_m[47u].x);
    precise float _156 = _150 * _155;
    precise float _157 = _155 * _151;
    precise float _158 = _155 * _152;
    precise float _159 = _149 + _156;
    precise float _160 = _149 + _157;
    precise float _161 = _149 + _158;
    precise float _167 = asfloat(cb0_m[46u].z);
    precise float _170 = asfloat(cb0_m[46u].w);
    precise float _171 = saturate(_159) * _167;
    precise float _172 = _167 * saturate(_160);
    precise float _173 = _167 * saturate(_161);
    precise float _174 = _171 + _170;
    precise float _175 = _170 + _172;
    precise float _176 = _170 + _173;
    float _178 = dp3_f32(float3(_145, _146, _147), 0.3333333432674407958984375f.xxx);
    precise float _179 = _145 - _178;
    precise float _180 = _146 - _178;
    precise float _181 = _147 - _178;
    precise float _184 = asfloat(cb0_m[49u].x);
    precise float _185 = _179 * _184;
    precise float _186 = _184 * _180;
    precise float _187 = _184 * _181;
    precise float _188 = _178 + _185;
    precise float _189 = _178 + _186;
    precise float _190 = _178 + _187;
    precise float _193 = asfloat(cb0_m[48u].z);
    precise float _196 = asfloat(cb0_m[48u].w);
    precise float _197 = _188 * _193;
    precise float _198 = _193 * _189;
    precise float _199 = _193 * _190;
    precise float _200 = _197 + _196;
    precise float _201 = _196 + _198;
    precise float _202 = _196 + _199;
    precise float4 _211 = t1.Sample(s1, float2(v1.x, v1.y));
    precise float _215 = _200 * _211.x;
    precise float _216 = _211.y * _201;
    precise float _217 = _211.z * _202;
    precise float _218 = _174 + _215;
    precise float _219 = _216 + _175;
    precise float _220 = _217 + _176;
    precise float _223 = asfloat(cb0_m[14u].y);
    precise float _224 = _218 * _223;
    precise float _225 = _223 * _219;
    precise float _226 = _223 * _220;
    precise float4 _238 = atex2D_Texture.Sample(asamp2D_Texture_s, float2(v0.x, v0.y));
    precise float _239 = _238.x;
    precise float _240 = _238.y;
    precise float _241 = _238.z;
    precise float _244 = asfloat(cb0_m[4u].x);
    precise float _245 = _239 + _244;
    precise float _246 = _240 + _244;
    precise float _247 = _241 + _244;
    precise float _248 = _245 * _239;
    precise float _249 = _240 * _246;
    precise float _250 = _241 * _247;
    precise float _251 = _248 + 1.0f;
    precise float _252 = _249 + 1.0f;
    precise float _253 = _250 + 1.0f;
    precise float _257 = _239 + sqrt(_251);
    precise float _258 = _240 + sqrt(_252);
    precise float _259 = _241 + sqrt(_253);
    precise float _276 = dp4_f32(_238, float4(asfloat(cb0_m[15u].x), asfloat(cb0_m[15u].y), asfloat(cb0_m[15u].z), asfloat(cb0_m[15u].w))) + asfloat(cb0_m[14u].w);
    o0.w = saturate(_276);
    precise float _280 = _257 - 1.0f;
    precise float _281 = _258 - 1.0f;
    precise float _282 = _259 - 1.0f;
    precise float _285 = asfloat(cb0_m[4u].w);
    precise float _286 = _285 * _280;
    precise float _287 = _285 * _281;
    precise float _288 = _285 * _282;
    precise float _289 = _200 * _286;
    precise float _290 = _287 * _201;
    precise float _291 = _288 * _202;
    precise float _294 = asfloat(cb0_m[12u].y);
    precise float _295 = _289 * _294;
    precise float _296 = _294 * _290;
    precise float _297 = _294 * _291;
    precise float _298 = _295 + 1.0f;
    precise float _299 = _296 + 1.0f;
    precise float _300 = _297 + 1.0f;
    precise float _303 = asfloat(cb0_m[12u].z);
    precise float _304 = _295 * _303;
    precise float _305 = _303 * _296;
    precise float _306 = _303 * _297;
    precise float _307 = _304 + 1.0f;
    precise float _308 = _305 + 1.0f;
    precise float _309 = _306 + 1.0f;
    precise float _310 = _295 * _307;
    precise float _311 = _308 * _296;
    precise float _312 = _309 * _297;
    precise float _313 = _310 / _298;
    precise float _314 = _311 / _299;
    precise float _315 = _312 / _300;
    precise float _316 = saturate(_313);
    precise float _317 = saturate(_314);
    precise float _318 = saturate(_315);
    precise float _319 = 1.0f - _316;
    precise float _320 = 1.0f - _317;
    precise float _321 = 1.0f - _318;
    precise float _322 = _319 * saturate(_224);
    precise float _323 = _320 * saturate(_225);
    precise float _324 = _321 * saturate(_226);
    precise float _325 = _322 + _316;
    precise float _326 = _323 + _317;
    precise float _327 = _324 + _318;
    precise float _336 = asfloat(cb0_m[10u].x);
    precise float _337 = log2(max(_325, 5.9604644775390625e-08f)) * _336;
    precise float _338 = log2(max(_326, 5.9604644775390625e-08f)) * _336;
    precise float _339 = log2(max(_327, 5.9604644775390625e-08f)) * _336;
    precise float _340 = exp2(_337);
    precise float _341 = exp2(_338);
    precise float _342 = exp2(_339);
    precise float _345 = asfloat(cb0_m[11u].y);
    precise float _346 = _340 * _345;
    precise float _347 = _341 * _345;
    precise float _348 = _342 * _345;
    precise float _362 = t3.Sample(s3, float2(v4.x, v4.y)).x * asfloat(cb0_m[11u].x);
    precise float _363 = _346 + _362;
    precise float _364 = _347 + _362;
    precise float _365 = _348 + _362;
    precise float _368 = asfloat(cb0_m[11u].z);
    precise float _369 = _363 + _368;
    precise float _370 = _364 + _368;
    precise float _371 = _365 + _368;
    precise float _372 = _340 + _369;
    precise float _373 = _370 + _341;
    precise float _374 = _371 + _342;
    o0.x = _372;
    o0.y = _373;
    o0.z = _374;
}