#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

Texture2D<float4> SourceTexture : register(t0);
// All permutations have color grading (LUT) (and the scene texture)
#if _FA1EB89D
Texture2D<float4> InternalGradingLUT : register(t1);
#elif _A8D65F39
Texture2D<float4> BloomTexture : register(t1);
Texture2D<float4> InternalGradingLUT : register(t2);
#elif _5A0FD042
Texture2D<float4> BloomTexture : register(t1);
Texture2D<float4> InternalGradingLUT : register(t2);
Texture2D<float4> HalfResSourceTexture : register(t3);
#elif _DB8E089A
Texture2D<float4> BloomTexture : register(t1);
Texture2D<float4> VignetteTexture : register(t2);
Texture2D<float4> InternalGradingLUT : register(t3);
#elif _331779B3
Texture2D<float4> BloomTexture : register(t1);
Texture2D<float4> InternalGradingLUT : register(t2);
Texture2D<float4> MaskTexture : register(t3);
#elif _37BB5F3B
Texture2D<float4> HalfResSourceTexture : register(t4);
Texture2D<float4> InternalGradingLUT : register(t3);
Texture2D<float4> VignetteTexture : register(t2);
Texture2D<float4> BloomTexture : register(t1);
#elif _3A60763D
Texture2D<float4> MaskTexture : register(t3);
Texture2D<float4> HalfResSourceTexture : register(t2);
Texture2D<float4> InternalGradingLUT : register(t1);
#elif _3B79940A
Texture2D<float4> MaskTexture : register(t4);
Texture2D<float4> HalfResSourceTexture : register(t3);
Texture2D<float4> InternalGradingLUT : register(t2);
Texture2D<float4> VignetteTexture : register(t1);
#elif _486FAF9A
Texture2D<float4> MaskTexture : register(t4);
Texture2D<float4> HalfResSourceTexture : register(t3);
Texture2D<float4> InternalGradingLUT : register(t2);
Texture2D<float4> BloomTexture : register(t1);
#elif _681DD226
Texture2D<float4> MaskTexture2 : register(t6);
Texture2D<float4> MaskTexture : register(t5);
Texture2D<float4> HalfResSourceTexture : register(t4);
Texture2D<float4> InternalGradingLUT : register(t3);
Texture2D<float4> VignetteTexture : register(t2);
Texture2D<float4> BloomTexture : register(t1);
#elif _9B9CCB1B
Texture2D<float4> MaskTexture2 : register(t5);
Texture2D<float4> MaskTexture : register(t4);
Texture2D<float4> HalfResSourceTexture : register(t3);
Texture2D<float4> InternalGradingLUT : register(t2);
Texture2D<float4> BloomTexture : register(t1);
#elif _E17B54F4
Texture2D<float4> MaskTexture : register(t5);
Texture2D<float4> HalfResSourceTexture : register(t4);
Texture2D<float4> InternalGradingLUT : register(t3);
Texture2D<float4> VignetteTexture : register(t2);
Texture2D<float4> BloomTexture : register(t1);
#elif _EAD71346
Texture2D<float4> HalfResSourceTexture : register(t2);
Texture2D<float4> InternalGradingLUT : register(t1);
#endif
SamplerState sampler0 : register(s0); // Bilinear

#if _DB8E089A || _37BB5F3B || _3B79940A || _681DD226 || _E17B54F4
#define VIGNETTE 1
#else
#define VIGNETTE 0
#endif

#if _331779B3 || _3A60763D || _3B79940A || _486FAF9A || _E17B54F4 || _681DD226 || _9B9CCB1B
#if _681DD226 || _9B9CCB1B
#define MASK 2
#else
#define MASK 1
#endif
#else
#define MASK 0
#endif

// TODO: find the remaining matching non FXAA permutations (some combinations are missing, though they might never be used by the game!)
#if _5A0FD042 || _37BB5F3B || _3A60763D || _3B79940A || _486FAF9A || _681DD226 || _9B9CCB1B || _E17B54F4 || _EAD71346
#define FXAA 1
#else
#define FXAA 0
#endif

#if _A8D65F39 || _DB8E089A || _5A0FD042 || _331779B3 || _37BB5F3B || _486FAF9A || _681DD226 || _9B9CCB1B || _E17B54F4
#define BLOOM 1
#else
#define BLOOM 0
#endif

// The base params are the same, Bloom and Vignette shaders just add more on top
cbuffer cb0 : register(b0)
{
  float4 cb0[150];
}

float3 GetSceneColor(float2 inCoords, Texture2D<float4> _texture, SamplerState _sampler)
{
  const float sampleBias = cb0[21].x; // Expected to be zero, though it could be used by the game to do a very ugly game blur effect
  float3 sceneColor = _texture.SampleBias(_sampler, inCoords.xy, sampleBias).rgb;

#if MASK >= 1
  float4 maskColor = MaskTexture.SampleBias(_sampler, inCoords.xy, sampleBias).rgba; // Note sure when this is used exactly
  sceneColor = lerp(sceneColor, maskColor.rgb, maskColor.a);
#if MASK >= 2
  maskColor = MaskTexture2.SampleBias(_sampler, inCoords.xy, sampleBias).rgba; // Note sure when this is used exactly
  sceneColor = lerp(sceneColor, maskColor.rgb, maskColor.a);
#endif
#endif

#if FXAA
  float3 halfResSceneColor = HalfResSourceTexture.SampleBias(_sampler, inCoords.xy, sampleBias).rgb; // This was R10G10B10A2_UNORM stored in linear space // Note: hardcoded the texture...
  if (abs(GetLuminance(sceneColor) - GetLuminance(halfResSceneColor)) > 0.02) // Luma: some optimization branch they do to skip FXAA if the full res and half res colors were close enough (it might help preserve texture detail too)
  {
    float4 r0,r1,r2,r3,r4,r5;
    int4 r1i, r2i, r3i, r4i;
    r0.xyz = sceneColor;
    r1.xyzw = cb0[149].xyxy * inCoords.xyxy;
    r1i.xyzw = (int4)r1.xyzw;
    r2i.xyzw = r1i.zwzw + int4(-1,-1,1,-1);
    r2.xyzw = (float4)r2i.xyzw;
    r3.xyzw = float4(-1,-1,-1,-1) + cb0[149].xyxy;
    r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
    r2.xyzw = min(r2.xyzw, r3.xyzw);
    r2i.xyzw = (int4)r2.zwxy;
    r4i.xy = r2i.zw;
    r4i.zw = 0;
    r4.w = 0;
    r4.xyz = _texture.Load(r4i.xyz).xyz;
    r2i.zw = 0;
    r2.w = 0;
    r2.xyz = _texture.Load(r2i.xyz).xyz;
    r1i.xyzw = r1i.xyzw + int4(-1,1,1,1);
    r1.xyzw = (float4)r1i.xyzw;
    r1.xyzw = max(float4(0,0,0,0), r1.xyzw);
    r1.xyzw = min(r1.xyzw, r3.xyzw);
    r1i.xyzw = (int4)r1.zwxy;
    r3i.xy = r1i.zw;
    r3i.zw = 0;
    r3.w = 0;
    r3.xyz = _texture.Load(r3i.xyz).xyz;
    r1i.zw = 0;
    r1.w = 0;
    r1.xyz = _texture.Load(r1i.xyz).xyz;
#if 0 // Luma: removed saturates
    r4.xyz = saturate(r4.xyz);
    r2.xyz = saturate(r2.xyz);
    r3.xyz = saturate(r3.xyz);
    r1.xyz = saturate(r1.xyz);
    r0.xyz = saturate(r0.xyz);
#endif
    r0.w = GetLuminance(r4.xyz);
    r1.w = GetLuminance(r2.xyz);
    r2.x = GetLuminance(r3.xyz);
    r1.x = GetLuminance(r1.xyz);
    r1.y = GetLuminance(r0.xyz);
    r1.z = r1.w + r0.w;
    r2.y = r2.x + r1.x;
    r2.y = -r2.y + r1.z;
    r3.xz = -r2.yy;
    r2.z = r2.x + r0.w;
    r2.w = r1.w + r1.x;
    r3.yw = r2.zz + -r2.ww;
    r1.z = r1.z + r2.x;
    r1.z = r1.z + r1.x;
    r1.z = 0.125 * r1.z;
    r1.z = max(0.0078125, r1.z);
    r2.y = min(abs(r3.w), abs(r2.y));
    r1.z = r2.y + r1.z;
    r1.z = rcp(r1.z);
    r3.xyzw = r3.xyzw * r1.zzzz;
    r3.xyzw = max(float4(-8,-8,-8,-8), r3.xyzw);
    r3.xyzw = min(float4(8,8,8,8), r3.xyzw);
    r3.xyzw = cb0[149].zwzw * r3.xyzw;
    r4.xyzw = r3.zwzw * float4(-0.5,-0.5,-0.166666672,-0.166666672) + inCoords.xyxy;
    r2.yzw = _texture.SampleBias(_sampler, r4.xy, cb0[21].x).xyz;
    r4.xyz = _texture.SampleBias(_sampler, r4.zw, cb0[21].x).xyz;
    r3.xyzw = r3.xyzw * float4(0.166666672,0.166666672,0.5,0.5) + inCoords.xyxy;
    r5.xyz = _texture.SampleBias(_sampler, r3.xy, cb0[21].x).xyz;
    r3.xyz = _texture.SampleBias(_sampler, r3.zw, cb0[21].x).xyz;
#if 0 // Luma: removed saturates
    r2.yzw = saturate(r2.yzw);
    r4.xyz = saturate(r4.xyz);
    r5.xyz = saturate(r5.xyz);
    r3.xyz = saturate(r3.xyz);
#endif
    r4.xyz = r5.xyz + r4.xyz;
    r5.xyz = float3(0.5,0.5,0.5) * r4.xyz;
    r2.yzw = r3.xyz + r2.yzw;
    r2.yzw = float3(0.25,0.25,0.25) * r2.yzw;
    r2.yzw = r4.xyz * float3(0.25,0.25,0.25) + r2.yzw;
    r1.z = GetLuminance(r2.yzw);
    r3.x = min(r2.x, r1.w);
    r3.x = min(r3.x, r1.x);
    r3.y = min(r1.y, r0.w);
    r3.x = min(r3.y, r3.x);
    r1.w = max(r2.x, r1.w);
    r1.x = max(r1.w, r1.x);
    r0.w = max(r1.y, r0.w);
    r0.w = max(r0.w, r1.x);
    r1.x = (r1.z < r3.x);
    r0.w = (r0.w < r1.z);
    r0.w = asfloat(asint(r0.w) | asint(r1.x));
    sceneColor = r0.w ? r5.xyz : r2.yzw;
  }
#endif // FXAA
  return sceneColor;
}

float3 ApplyBloom(float2 inCoords, float3 color, Texture2D<float4> _texture, SamplerState _sampler)
{
  const float sampleBias = cb0[21].x; // Expected to be zero, though it could be used by the game to do a very ugly game blur effect
  const float3 bloomColor = _texture.SampleBias(_sampler, inCoords.xy, sampleBias).rgb;
  const float bloomStrength = cb0[142].z; // Expected to be > 0 and < 1
  return color + (bloomColor * bloomStrength);
}

float3 ApplyExposure(float3 color)
{
  const float exposure = cb0[130].w;
  return color * exposure; // Luma: removed saturate()
}

float3 ApplyLUT(float3 color, Texture2D<float4> _texture, SamplerState _sampler)
{
  float3 postLutColor;

  bool lutExtrapolation = true;
#if DEVELOPMENT
  lutExtrapolation = LumaSettings.DevSetting01 <= 0.5;
#endif
  if (lutExtrapolation)
  {
    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = color.rgb;
    extrapolationData.vanillaInputColor = saturate(color.rgb);
  
    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.lutSize = round(1.0 / cb0[130].y);
    // Empirically found value for Prey. Anything less will be too compressed, anything more won't have a noticieable effect.
    // This helps keep the extrapolated LUT colors at bay, avoiding them being overly saturated or overly desaturated.
    // At this point, Prey can have colors with brightness beyond 35000 nits, so obviously they need compressing.
    //extrapolationSettings.inputTonemapToPeakWhiteNits = 1000.0; // Relative to "extrapolationSettings.whiteLevelNits" // NOT NEEDED UNTIL PROVEN OTHERWISE
    // Empirically found value for Prey. This helps to desaturate extrapolated colors more towards their Vanilla (HDR tonemapper but clipped) counterpart, often resulting in a more pleasing and consistent look.
    // This can sometimes look worse, but this value is balanced to avoid hue shifts.
    //extrapolationSettings.clampedLUTRestorationAmount = 1.0 / 4.0; // NOT NEEDED UNTIL PROVEN OTHERWISE
    // The LUT was R11G11B10_FLOAT, which is pretty weird
    extrapolationSettings.inputLinear = true;
    extrapolationSettings.lutInputLinear = true;
    extrapolationSettings.lutOutputLinear = true;
    extrapolationSettings.outputLinear = true;
#if 1 // High quality. Not particularly needed in this game as most LUTs are neutral, but it won't hurt.
    extrapolationSettings.samplingQuality = 2;
    extrapolationSettings.extrapolationQuality = 2;
#endif
  
    postLutColor = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);
  }
  else
  {
    // This code looks weird, but it is the standard 2D LUT sampling math, just done with a slightly different order for the math
    const float lutMax = cb0[130].z; // The 3D LUT max: "LUT_SIZE - 1"
    const float2 lutInvSize = cb0[130].xy; // The 3D LUT size (before unwrapping it): "1 / LUT_SIZE", likely equal in value on x and y
    const float2 lutCoordsOffset = float2(0.5, 0.5) * lutInvSize; // The uv bias: "0.5 / LUT_SIZE"
    float3 lutTempCoords3D = saturate(color) * lutMax;
    float2 lutCoords2D = (lutTempCoords3D.xy * lutInvSize) + lutCoordsOffset;
    float lutSliceIdx = floor(lutTempCoords3D.z);
    // Offset the horizontal axis by the index of z (blue) slice
    lutCoords2D.x += lutSliceIdx * lutInvSize.y;
    float lutSliceFrac = lutTempCoords3D.z - lutSliceIdx;
    float3 lutColor1 = _texture.SampleLevel(_sampler, lutCoords2D, 0).rgb;
    // Sample the next slice
    float3 lutColor2 = _texture.SampleLevel(_sampler, lutCoords2D + float2(lutInvSize.y, 0), 0).rgb;
    // Blend the two slices with the z (blue) ratio
    postLutColor = lerp(lutColor1, lutColor2, lutSliceFrac);
    
    float hueRestoration = 0.0;
    bool restorePostProcessInBT2020 = true;
#if DEVELOPMENT
    hueRestoration = LumaSettings.DevSetting02;
    restorePostProcessInBT2020 = LumaSettings.DevSetting03 <= 0.5;
#endif
    postLutColor = RestorePostProcess(color, saturate(color), postLutColor, hueRestoration, restorePostProcessInBT2020);
  }
  
  return postLutColor;
}

float3 Tonemap(float3 color)
{
  DICESettings config = DefaultDICESettings(DICE_TYPE_BY_CHANNEL_PQ); // Do DICE by channel to desaturate highlights and keep the SDR range unotuched
  float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
#if 0 // Test: make PQ tonemapping indepdenent from the user paper white (the result seems about identical if we start the shoulder from paper white), this isn't what the design intended
  peakWhite /= paperWhite;
  paperWhite = 1.0;
#endif
#if 0 // Disabled as it makes highlights weaker // TODO: investigate in all games. Is this a good thing?
  config.ShoulderStart = paperWhite / peakWhite; // Start tonemapping beyond paper white, so we leave the SDR range untouched (roughly, given that this tonemaps in BT.2020)
#endif
  return DICETonemap(color * paperWhite, peakWhite, config) / paperWhite;
}

float3 ApplyVignette(float2 inCoords, float3 color, Texture2D<float4> _texture, SamplerState _sampler)
{
  const float sampleBias = cb0[21].x; // Expected to be zero, though it could be used by the game to do a very ugly game blur effect
  float4 vignette;
  vignette.rgba = _texture.SampleBias(_sampler, inCoords.xy, sampleBias).rgba;
  vignette.rgb = vignette.rgb * cb0[144].rgb + -color.rgb;
  float alpha = cb0[144].w * vignette.a;
  return color.rgb + vignette.rgb * alpha;
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  const float2 inCoords = v1.xy;

  float3 color;
  color = GetSceneColor(inCoords, SourceTexture, sampler0);
#if BLOOM
  color = ApplyBloom(inCoords, color, BloomTexture, sampler0);
#endif
  color = ApplyExposure(color);
  color = ApplyLUT(color, InternalGradingLUT, sampler0);
  color = Tonemap(color); // Added by Luma. the game was just clipping
#if VIGNETTE
  color = ApplyVignette(inCoords, color, VignetteTexture, sampler0);
#endif

#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
	ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
  color.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
	ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

  outColor = float4(color.rgb, 1.0);
}