#define LUT_3D 1

#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Tonemap.hlsl"

cbuffer _Globals : register(b0)
{
  float ImageSpace_hlsl_ToneMapPixelShader0000000000000000000000000000000b_4bits : packoffset(c0) = {0};
  float4 fogColor : packoffset(c1);
  float3 fogTransform : packoffset(c2);
  float2 fogLuminance : packoffset(c3);
  row_major float3x4 screenDataToCamera : packoffset(c4);
  float globalScale : packoffset(c7);
  float sceneDepthAlphaMask : packoffset(c7.y);
  float globalOpacity : packoffset(c7.z);
  float distortionBufferScale : packoffset(c7.w);
  float3 wToZScaleAndBias : packoffset(c8);
  float4 screenTransform[2] : packoffset(c9);
  float4 textureToPixel : packoffset(c11);
  float4 pixelToTexture : packoffset(c12);
  float maxScale : packoffset(c13) = {0};
  float bloomAlpha : packoffset(c13.y) = {0};
  float sceneBias : packoffset(c13.z) = {1};
  float3 gammaSettings : packoffset(c14);
  float exposure : packoffset(c14.w) = {0};
  float deltaExposure : packoffset(c15) = {0};
  float4 ColorFill : packoffset(c16);
  float2 LowResTextureDimensions : packoffset(c17);
  float2 DownsizeTextureDimensions : packoffset(c17.z);
}

SamplerState s_framebuffer_s : register(s0);
SamplerState s_bloom_s : register(s1);
SamplerState s_toneMapTable_s : register(s2);
Texture2D<float4> s_framebuffer : register(t0);
Texture2D<float4> s_bloom : register(t1);
Texture3D<float4> s_toneMapTable : register(t2); // LUT

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = s_bloom.Sample(s_bloom_s, w1.xy).xyzw;
  r1.xyzw = s_framebuffer.Sample(s_framebuffer_s, v1.xy).xyzw;
  r0.xyz = r0.xyz * bloomAlpha + r1.xyz;
  outColor.w = saturate(r1.w); // Keep the saturate on alpha, it's probably pointless anyway
  r0.xyz *= sceneBias;
#if 0 // Vanilla
  r0.xyz = pow(abs(r0.xyz), 1.0 / 2.2) * sign(r0.xyz);
  float3 clippedLuminance = r0.xyz - saturate(r0.xyz);
  outColor.xyz = s_toneMapTable.Sample(s_toneMapTable_s, saturate(r0.xyz)).xyz + clippedLuminance;
#elif 1 // HDR LUT Extrapolation
    float clippedAmount = 0.5 / 16.0;
    float clippedPoint = 2.5 / 16.0;
    r0.rgb *= lerp(0.333, 1.0, saturate(r0.rgb / clippedAmount));
    //r0.rgb *= lerp(0.333, 1.0, saturate(GetLuminance(r0.rgb) / clippedAmount));

    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = r0.rgb;

    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
    extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
    extrapolationSettings.lutSize = 0;
    
    // DH2 is all linear
    extrapolationSettings.inputLinear = true;
    extrapolationSettings.lutInputLinear = false;
    extrapolationSettings.lutOutputLinear = false;
    extrapolationSettings.outputLinear = false;

    // Intermediary gamma correction through LUT
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
    extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;

    // apply extrapolated LUT to HDR
    outColor.rgb = SampleLUTWithExtrapolation(s_toneMapTable, s_toneMapTable_s, extrapolationData, extrapolationSettings);
#else
  outColor.xyz = pow(abs(r0.xyz), 1.0 / 2.2) * sign(r0.xyz);
#endif
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    outColor.rgb = Tonemap_DICE(outColor.rgb * paperWhite, peakWhite) / paperWhite;
}