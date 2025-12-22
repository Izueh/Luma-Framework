#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/DICE.hlsl"

cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

cbuffer cbConsts : register(b1)
{
  float4 Consts[17] : packoffset(c0);
}

// Heat Haze + DoF are always enabled if Displacement/Cauistics are
#if _01F41F2D || _0C35F299 || _148CD952 || _2319D5A4 || _371AD4D5 || _3753CA0A || _4030BF6E || _49704266 || _6A1C711F || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _8D8F7072 || _96DA986B || _9C62A6F9 || _A274F081 || _A91F8AB9 || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D4B1C6E9 || _DC0FE377 || _DED46AD7 || _F4E80E62 || _FA0676EF || _FDBDB73F
#define HEAT_HAZE_AND_DOF 1
#endif
#if _148CD952 || _371AD4D5 || _3753CA0A || _4030BF6E || _6A1C711F || _75190444 || _79193F1D || _8610E7F5 || _8D59471A || _8D8F7072 || _A91F8AB9 || _C16B4E6B || _D4B1C6E9 || _DC0FE377 || _F4E80E62
#define DISPLACEMENT_AND_CAUSTICS 1
#endif

// These two apparently also always come together (they are probably both part of the same permutation in the original code)
#if _01F41F2D || _0BAC4255 || _0C35F299 || _148CD952 || _15BC0ABC || _1C087BA1 || _2319D5A4 || _2607E7C0 || _288B16D3 || _2FB48E77 || _371AD4D5 || _3753CA0A || _38ABE9E7 || _3EC5DBB9 || _4030BF6E || _49704266 || _4A9BFEC5 || _6A1C711F || _6C0BCB6B || _6F6BFEDA || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _8D8F7072 || _92550B56 || _96DA986B || _9C62A6F9 || _9D857B42 || _A274F081 || _A91CF149 || _A91F8AB9 || _A9CEF67D || _ADAFB4CD || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D0F9B11B || _D4B1C6E9 || _DC0FE377 || _DED46AD7 || _E1ECF661 || _F21C9CBA || _F4E80E62 || _FA0676EF || _FA796E93 || _FDBDB73F
#define EXPOSURE 1
#define COLOR_MULT 1
#endif

// These two always come together (if the bloom is true, then blur is implied)?
#if _01F41F2D || _0C35F299 || _148CD952 || _2319D5A4 || _371AD4D5 || _3753CA0A || _4030BF6E || _49704266 || _6A1C711F || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _8D8F7072 || _96DA986B || _9C62A6F9 || _A274F081 || _A91F8AB9 || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D4B1C6E9 || _DC0FE377 || _DED46AD7 || _F4E80E62 || _FA0676EF || _FDBDB73F
#define BLUR 1
#endif
#if _01F41F2D || _0BAC4255 || _0C35F299 || _148CD952 || _15BC0ABC || _1C087BA1 || _2319D5A4 || _288B16D3 || _371AD4D5 || _3753CA0A || _3EC5DBB9 || _4030BF6E || _49704266 || _4A9BFEC5 || _6A1C711F || _6C0BCB6B || _6F6BFEDA || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _8D8F7072 || _92550B56 || _96DA986B || _9C62A6F9 || _9D857B42 || _A274F081 || _A91CF149 || _A91F8AB9 || _A9CEF67D || _ADAFB4CD || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D0F9B11B || _D4B1C6E9 || _DC0FE377 || _DED46AD7 || _E1ECF661 || _F21C9CBA || _F4E80E62 || _FA0676EF || _FDBDB73F
#define BLOOM 1
#endif

#if _0C35F299 || _148CD952 || _15BC0ABC || _288B16D3 || _371AD4D5 || _3EC5DBB9 || _6A1C711F || _6F6BFEDA || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _92550B56 || _96DA986B || _9C62A6F9 || _9D857B42 || _A274F081 || _A91CF149 || _A91F8AB9 || _A9CEF67D || _BF1F1C29 || _C16B4E6B || _DED46AD7 || _E1ECF661 || _F4E80E62 || _FA0676EF
#define VIGNETTE 1
#endif

// Tonemap LUT is always true if there's a fade LUT
#if _0C35F299 || _148CD952 || _15BC0ABC || _1C087BA1 || _2319D5A4 || _288B16D3 || _371AD4D5 || _3753CA0A || _3EC5DBB9 || _4A9BFEC5 || _6A1C711F || _6C0BCB6B || _6F6BFEDA || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _92550B56 || _96DA986B || _9C62A6F9 || _9D857B42 || _A274F081 || _A91CF149 || _A91F8AB9 || _A9CEF67D || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D4B1C6E9 || _DED46AD7 || _E1ECF661 || _F4E80E62 || _FA0676EF
#define COLOR_GRADING_LUT 1
#endif
#if _0C35F299 || _371AD4D5 || _3EC5DBB9 || _8610E7F5 || _92550B56 || _9D857B42 || _DED46AD7 || _F4E80E62 || _FA0676EF
#define FADE_LUT 1
#endif

#if _0C35F299 || _148CD952 || _15BC0ABC || _371AD4D5 || _3EC5DBB9 || _6F6BFEDA || _79193F1D || _7F138E1C || _8610E7F5 || _92550B56 || _96DA986B || _9D857B42 || _A91F8AB9 || _BF1F1C29 || _DED46AD7 || _E1ECF661 || _F4E80E62 || _FA0676EF
#define FILM_GRAIN 1
#endif

#if _01F41F2D || _0BAC4255 || _0C35F299 || _148CD952 || _15BC0ABC || _1C087BA1 || _2319D5A4 || _288B16D3 || _371AD4D5 || _3753CA0A || _3EC5DBB9 || _4030BF6E || _49704266 || _4A9BFEC5 || _6A1C711F || _6C0BCB6B || _6F6BFEDA || _75190444 || _79193F1D || _7CF7827A || _7F138E1C || _8610E7F5 || _87F34BAA || _8D59471A || _8D8F7072 || _92550B56 || _96DA986B || _9C62A6F9 || _9D857B42 || _A274F081 || _A91CF149 || _A91F8AB9 || _A9CEF67D || _ADAFB4CD || _BCF2BA69 || _BF1F1C29 || _C16B4E6B || _D0F9B11B || _D4B1C6E9 || _DC0FE377 || _DED46AD7 || _E1ECF661 || _F21C9CBA || _F4E80E62 || _FA0676EF || _FDBDB73F
#define USER_BRIGHTNESS_CALIBRATION 1
#endif

// "0x0BAC4255", "0x38ABE9E7" and "0x83CC89FB" are the only ones that don't define a TONEMAP_TYPE
#if _01F41F2D || _0C35F299 || _148CD952 || _15BC0ABC || _1C087BA1 || _3EC5DBB9 || _4030BF6E || _75190444 || _8D59471A || _9C62A6F9 || _A9CEF67D || _ADAFB4CD || _BCF2BA69 || _BF1F1C29 || _F4E80E62 || _FA796E93
#define TONEMAP_TYPE 1
#elif _2319D5A4 || _2FB48E77 || _371AD4D5 || _3753CA0A || _6C0BCB6B || _6F6BFEDA || _79193F1D || _7F138E1C || _9D857B42 || _A274F081 || _A91CF149 || _C16B4E6B || _D0F9B11B || _DC0FE377 || _FA0676EF || _FDBDB73F
#define TONEMAP_TYPE 2
#elif _2607E7C0 || _288B16D3 || _49704266 || _4A9BFEC5 || _6A1C711F || _7CF7827A || _8610E7F5 || _87F34BAA || _8D8F7072 || _92550B56 || _96DA986B || _A91F8AB9 || _D4B1C6E9 || _DED46AD7 || _E1ECF661 || _F21C9CBA
#define TONEMAP_TYPE 3
#endif

// Note: permutation "0x83CC89FB" has all the features off (passthrough)
#ifndef HEAT_HAZE_AND_DOF
#define HEAT_HAZE_AND_DOF 0
#endif
#ifndef DISPLACEMENT_AND_CAUSTICS
#define DISPLACEMENT_AND_CAUSTICS 0
#endif
#ifndef EXPOSURE
#define EXPOSURE 0
#endif
#ifndef COLOR_MULT
#define COLOR_MULT 0
#endif
#ifndef BLUR
#define BLUR 0
#endif
#ifndef BLOOM
#define BLOOM 0
#endif
#ifndef BLUR
#define BLUR 0
#endif
#ifndef VIGNETTE
#define VIGNETTE 0
#endif
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 0
#endif
#ifndef COLOR_GRADING_LUT
#define COLOR_GRADING_LUT 0
#endif
#ifndef FADE_LUT
#define FADE_LUT 0
#endif
#ifndef FILM_GRAIN
#define FILM_GRAIN 0
#endif
#ifndef USER_BRIGHTNESS_CALIBRATION
#define USER_BRIGHTNESS_CALIBRATION 0
#endif

SamplerState SceneTexture_s : register(s0); // Clamp linear sampler
SamplerState BlurredSceneTexture_s : register(s1); // Clamp linear sampler
SamplerState DepthTexture_s : register(s2); // Clamp linear sampler
SamplerState BloomTexture_s : register(s3); // Clamp linear sampler
SamplerState SecondaryBloomTexture_s : register(s4); // Clamp linear sampler
SamplerState DisplacementMap_s : register(s5);
SamplerState CausticsMap_s : register(s6);
SamplerState FilmGrainTexture_s : register(s8);
SamplerState EdgeFadeTexture_s : register(s9); // Clamp linear sampler
SamplerState LensDirtTexture_s : register(s10); // Wrap linear sampler
SamplerState ColorCorrectionTexture_s : register(s11); // Clamp linear sampler
SamplerState ColorCorrectionTextureFade_s : register(s12);
SamplerState HeatHazeTexture_s : register(s13); // Wrap linear sampler
SamplerState BokehFocusTexture_s : register(s14); // Clamp linear sampler
Texture2D<float4> SceneTexture : register(t0);
Texture2D<float4> BlurredSceneTexture : register(t1);
Texture2D<float> DepthTexture : register(t2);
Texture2D<float4> BloomTexture : register(t3);
Texture2D<float4> SecondaryBloomTexture : register(t4);
Texture2D<float4> DisplacementMap : register(t5);
Texture2D<float4> CausticsMap : register(t6);
Texture2D<float4> FilmGrainTexture : register(t8);
Texture2D<float4> EdgeFadeTexture : register(t9);
Texture2D<float4> LensDirtTexture : register(t10);
Texture3D<float4> ColorCorrectionTexture : register(t11); // 32x 3D LUT
Texture3D<float4> ColorCorrectionTextureFade : register(t12);
Texture2D<float4> HeatHazeTexture : register(t13);
Texture2D<float> BokehFocusTexture : register(t14);

float3 Tonemap(float3 postProcessedColor)
{
  float3 tonemappedColor = postProcessedColor;
#if TONEMAP_TYPE == 1
  float postProcessedColorLuminance = GetLuminance(postProcessedColor);
#if 0 // Luma: disabled, it's unnecessary (it'd only be a problem if luminance was -1, which is never the case...)
  postProcessedColorLuminance = max(9.99999975e-005, postProcessedColorLuminance);
#endif
  float postProcessedColorTonemapRatio = ((postProcessedColorLuminance / sqr(Consts[10].x)) + 1.0) / (postProcessedColorLuminance + 1.0);
  tonemappedColor = postProcessedColor * postProcessedColorTonemapRatio;
#elif TONEMAP_TYPE == 2
  float3 r0,r2,r3;
  r0.xyz = Consts[10].x * postProcessedColor;
  r0.xyz = Consts[10].z * Consts[10].y + r0.xyz;
  r0.xyz = postProcessedColor * r0.xyz + (Consts[11].x * Consts[10].w);
  r3.xyz = Consts[10].x * postProcessedColor + Consts[10].y;
  r2.xyz = postProcessedColor * r3.xyz + (Consts[11].y * Consts[10].w);
  r0.xyz = r0.xyz / r2.xyz;
  tonemappedColor = r0.xyz - (Consts[11].x / Consts[11].y);
#elif TONEMAP_TYPE == 3
  // Logarithmic tonemapping (at first I thought this was a lut encoding formula to then do TM in the LUT, but that doesn't seem to be the case).
  // "Consts[10].x" is usually 16, so:
  // this maps <0 to <0, 0 to 0, 0.5 to 0.99609375 and 1 to ~0.99999.
  // This seems to be extremely unoptimized, as it's not perceptual at all.
  tonemappedColor = 1.0 - exp2(postProcessedColor * -Consts[10].x);
#endif
  return tonemappedColor;
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2,r3;

  float2 unscaledUV = v1.xy;
#if DISPLACEMENT_AND_CAUSTICS
  unscaledUV += (DisplacementMap.Sample(DisplacementMap_s, v1.zw).xy * 2.0 - 1.0) * 0.01;
#endif // DISPLACEMENT_AND_CAUSTICS
  float2 uv = unscaledUV * Consts[16].xy;
  float2 lensDirtUV = unscaledUV;

#if HEAT_HAZE_AND_DOF
  float depth = DepthTexture.Sample(DepthTexture_s, uv).x;
  float linearDepth = depth * Consts[2].z + Consts[2].w;
  float inverseLinearDepth = 1.0 / linearDepth;

  r2.xyzw = uv.x * Consts[5].xyzw + Consts[6].xyzw * uv.y + depth * Consts[7].xyzw + Consts[8].xyzw;
  r2.xyz = r2.xyz / r2.w;
  float3 someDepthVar = r2.xyz;
  r2.xyz = r2.xzy - Globals[4].xzy; // Werid swizzle
  r0.z = rsqrt(dot(r2.xyz, r2.xyz));
  r2.xyz = r2.zxy * r0.z; // Werid swizzle

  if (0 < Consts[15].w) {
    r0.z = min(abs(r2.y), abs(r2.z));
    r0.w = max(abs(r2.y), abs(r2.z));
    r0.w = 1 / r0.w;
    r0.z = r0.z * r0.w;
    r0.w = r0.z * r0.z;
    r1.w = r0.w * 0.0208350997 + -0.0851330012;
    r1.w = r0.w * r1.w + 0.180141002;
    r1.w = r0.w * r1.w + -0.330299497;
    r0.w = r0.w * r1.w + 0.999866009;
    r1.w = r0.z * r0.w;
    r2.w = (abs(r2.z) < abs(r2.y));
    r1.w = r1.w * -2 + 1.57079637;
    r1.w = r2.w ? r1.w : 0;
    r0.z = r0.z * r0.w + r1.w;
    r0.w = (-r2.z < r2.z);
    r0.w = r0.w ? -3.141593 : 0;
    r0.z = r0.z + r0.w;
    r0.w = min(-r2.y, -r2.z);
    r1.w = max(-r2.y, -r2.z);
    r0.w = (r0.w < -r0.w);
    r1.w = (r1.w >= -r1.w);
    r0.w = r0.w ? r1.w : 0;
    r0.z = r0.w ? -r0.z : r0.z;
    r0.z = 3.14159012 + r0.z;
    r3.x = 0.159155071 * r0.z;
    r0.z = abs(r2.x) * 0.5 + 0.5;
    r3.y = -r2.x * r0.z;
    r0.z = saturate(Consts[15].z * inverseLinearDepth);
    r0.w = Consts[15].x * r0.z;
    r1.w = saturate(-r2.x);
    r1.w = r1.w * r1.w;
    r1.w = r1.w * Consts[15].y + 1;
    r1.w = 1 / r1.w;
    r0.w = r1.w * r0.w;
    r3.xyzw = -Consts[14].xyzw + r3.xyxy;
    r3.xyzw = float4(8,3,25,8) * r3.xyzw;
    r2.yz = HeatHazeTexture.Sample(HeatHazeTexture_s, r3.zw).yw;
    r2.yz = float2(-0.5,-0.5) + r2.yz;
    r2.yz = float2(0.75,0.75) * r2.yz;
    r3.xyz = HeatHazeTexture.Sample(HeatHazeTexture_s, r3.xy).xyw;
    r2.w = -0;
    r2.yzw = r3.xyz + r2.wyz;
    r2.yzw = float3(0,-0.5,-0.5) + r2.yzw;
    r3.xy = r2.wz * r0.w;
    lensDirtUV = r3.xy * r2.y + unscaledUV;
    r0.w = dot(r2.zw, r2.zw);
    r0.w = sqrt(r0.w);
    r0.w = r1.w * 0.300000012 + r0.w;
    r0.z = r0.w * r0.z;
    r0.z = r0.z * r1.w;
    r0.z = Consts[15].w * r0.z;
    r1.z = saturate(r0.z * r2.y);
    r1.y = r1.z;
  } else {
    r1.y = 0;
  }
#endif // HEAT_HAZE_AND_DOF

  float3 scene = SceneTexture.Sample(SceneTexture_s, uv).xyz;
  float3 blurredScene = BlurredSceneTexture.Sample(BlurredSceneTexture_s, uv).xyz;
  float3 bloomedScene = BloomTexture.Sample(BloomTexture_s, uv).xyz;
  float3 secondaryBloomedScene = SecondaryBloomTexture.Sample(SecondaryBloomTexture_s, uv).xyz;
  float3 lensDirt = LensDirtTexture.Sample(LensDirtTexture_s, lensDirtUV).xyz; // TODO: unstretch for UW?
#if 1 // Luma: NaNs protection (there's not supposed to be any negative colors!). // TODO: issues are caused by 0xA6DB19EC and 0x4B5F418D etc
  // Note that in the vanilla code, negative values would go through a sqrt and go nan (if textures were upgraded, otherwise they'd be R11G11B10_FLOAT, which has no negatives or NaNs), and then be turned into 1 with a min(x, 1), but here we turn them to 0 early, which is better.
  scene = max(scene, 0.0);
  blurredScene = max(blurredScene, 0.0);
  bloomedScene = max(bloomedScene, 0.0);
  secondaryBloomedScene = max(secondaryBloomedScene, 0.0);
#endif

  float3 postProcessedColor = scene;

#if BLUR
#if HEAT_HAZE_AND_DOF
  r2.x = saturate(r2.x);
  r0.z = r2.x * r2.x;
  r0.z = r0.z * Consts[13].x + 1;
  r0.w = saturate(inverseLinearDepth * Consts[4].x + -Consts[4].y);
  r0.z = r0.w / r0.z;
  r0.z = saturate(r0.z * Consts[0].w + Consts[0].z);
  r0.z = -0.1 + r0.z;
  r0.z = saturate(16 * r0.z);
  float bokeh = BokehFocusTexture.Sample(BokehFocusTexture_s, uv).x;
  r0.x = r0.z + bokeh + min(1, r1.y * 4.0);
  float blurIntensity = saturate(r0.x);
#else
  float blurIntensity = Consts[0].z;
#endif // HEAT_HAZE_AND_DOF

  // Note: the auto expose level is determined by the output brightness of this shader, so in HDR it'd be different. We semi fixed it in the AutoExposure shader, though I still haven't found where the final 1x1 exp is actually calculated, likely on the CPU
  postProcessedColor = lerp(scene, blurredScene, blurIntensity);
#endif

#if EXPOSURE
  // Note: this is self influenced by the tonemapper result of the previous frame(s)
  postProcessedColor *= Consts[2].x;
#endif

#if BLOOM
  postProcessedColor += (bloomedScene * Consts[3].x) + secondaryBloomedScene * (lensDirt * Consts[3].z + Consts[3].y);
#endif

#if 0 // Test: raw output
  o0.xyz = postProcessedColor;
  o0.w = sqrt(GetLuminance(o0.xyz));
  return;
#endif

#if VIGNETTE
  if (Consts[13].z == 0.0) {
    r1.xyz = EdgeFadeTexture.Sample(EdgeFadeTexture_s, v1.xy).xyz;
    r1.xyz = lerp(1.0, r1.xyz, Consts[2].y);
    postProcessedColor *= r1.xyz;
  } else {
    r2.xy = v1.xy * 2.0 - 1.0;
    r2.xy = abs(r2.xy) * 0.5 + 0.5;
    r2.xy = 1.0 - r2.xy;
    r2.xyz = EdgeFadeTexture.Sample(EdgeFadeTexture_s, r2.xy).xyz;
    r2.xyz = lerp(1.0, r2.xyz * r2.xyz, Consts[2].y);
    postProcessedColor *= r2.xyz;
  }
#endif

  // Pre-set this in case the branches below won't set it
  outColor.rgb = postProcessedColor;

#if COLOR_GRADING_LUT || FADE_LUT || FILM_GRAIN || TONEMAP_TYPE > 0 || COLOR_MULT // Any LUT

#if DEVELOPMENT
  // Match the mid grey brightness shift from the tonemapper+LUT onto the untonemapped color (so we can later blend them properly)
  float midGreyIn = MidGray;
#if COLOR_GRADING_LUT
#if 0 // Somehow this raises brightness way too much
  float midGreyLutIn = Tonemap(midGreyIn).x; // Ignore "Consts[1].xyz" as it might be used to do fade to blacks etc
#else // Optionally ignore the tonemapper completely
  float midGreyLutIn = midGreyIn;
#endif
  midGreyLutIn = midGreyLutIn * (1.0 - (1.0 / 32.0)) + (0.5 / 32.0);
  float3 midGreyLutOut3 = sqr(ColorCorrectionTexture.Sample(ColorCorrectionTexture_s, sqrt(saturate(midGreyLutIn))).xyz);
  float midGreyLutOut = average(midGreyLutOut3);
  float midGreyOut = midGreyLutOut;
#else // !COLOR_GRADING_LUT
  float midGreyOut = Tonemap(midGreyIn).x;
#endif // COLOR_GRADING_LUT
  float3 untonemapped = postProcessedColor * (midGreyOut / midGreyIn); // TODO: delete? This makes little sense unfortunately given grading and TM are bundled in the LUT
#endif // DEVELOPMENT

  float3 tonemappedColor = postProcessedColor;
  float3 tonemapperInColor = tonemappedColor;
  float3 tonemapperLostColor = 0;

  bool doHDR = !ShouldForceSDR(uv) && LumaSettings.DisplayMode == 1;

#if DEVELOPMENT
  if (uv.x >= DVS1) // TODO: delete both!
#endif // DEVELOPMENT
  {
#if TONEMAP_TYPE > 0
  float tonemapperClippingPoint = 0.05; // 0.05 seems good. 0.18 (mid grey) is too high. Anything below ~0.035 messes up the average colors.
#if DEVELOPMENT && 0
  tonemapperClippingPoint = DVS7; // TODO: restore the red/green/blue/white/magenta/yellow etc LUT grading, and expand the lost color based on how the TM+LUT altered mid grey, instead of the current rgb combination?
#endif // DEVELOPMENT
  tonemapperInColor = postProcessedColor;
#if 1 // Luma HDR
  if (doHDR)
    tonemapperInColor = min(tonemapperInColor, tonemapperClippingPoint);
#endif
  tonemapperLostColor = postProcessedColor - tonemapperInColor;
  tonemappedColor = Tonemap(tonemapperInColor);
#endif // TONEMAP_TYPE > 0

#if COLOR_MULT
  postProcessedColor *= Consts[1].xyz;
  tonemappedColor *= Consts[1].xyz; // "Consts[1].xyz" is usually 1, probably used for fades to black given that if it was > 1 it'd cause clipping
#endif // COLOR_MULT

#if DISPLACEMENT_AND_CAUSTICS
  r0.w = 1 - saturate(0.005 * (Globals[7].w - someDepthVar.y));
  r2.xyz = Globals[4].xyz - someDepthVar.xyz;
  r1.x = dot(r2.xyz, r2.xyz);
  r1.x = 0.002 * r1.x;
  r1.x = 1 / r1.x;
  r1.x = min(0.4, r1.x);
  r2.x = max(0.1, r0.w);
  r2.y = 0.2 + Globals[6].y;
  r2.y = max(0.1, r2.y);
  r2.y = 4 * r2.y;
  r2.y = min(1, r2.y);
  r1.y = r0.w * -7.8 + 8.9;
  r1.y = floor(r1.y);
  r3.xyzw = -someDepthVar.y * 0.25 + someDepthVar.xzxz;
  r3.xyzw = r3.xyzw / r1.y;
  r3.xyzw = float4(0.3,0.3,0.08,0.08) * r3.xyzw;
  r1.y = CausticsMap.Sample(CausticsMap_s, r3.xy).w;
  r1.z = CausticsMap.Sample(CausticsMap_s, r3.zw).w;
  r1.y = r1.y * 0.5 + r1.z;
  r1.y = r1.y + r1.y;
  r0.w = r0.w * 2 + 2;
  r1.x = r2.x * r1.x;
  r1.x = r1.x * r2.y;
  r1.y = log2(r1.y);
  r0.w = r1.y * r0.w;
  r0.w = exp2(r0.w);
  r0.w = r1.x * r0.w + 1;
  postProcessedColor *= r0.w;
  tonemappedColor *= r0.w;
#endif // DISPLACEMENT_AND_CAUSTICS
  }
#if DEVELOPMENT
  else
  {
    tonemappedColor *= Tonemap(midGreyIn).x / midGreyIn;
  }
#endif // DEVELOPMENT

#if COLOR_GRADING_LUT || FADE_LUT || FILM_GRAIN
  bool skipLUTEncoding = false;
#if DEVELOPMENT
  skipLUTEncoding = DVS3 > 0;
  if (uv.x >= DVS2)
#endif // DEVELOPMENT
  {
  // LUT is gamma 2.0 space (weird, not sure they whould have been authored that way, also the sqrt encoding doesn't go well alongside the log encoding from "TONEMAP_TYPE" 3, given it focuses the vast majority of the precision on highlights values).
  // Update: they change colors and contrast all across the range, but LUTs are clearly gamma in and gamma out, without any major changes or tonemapping inside of them.
  float3 lutEncodedColor = skipLUTEncoding ? tonemappedColor : sqrt(saturate(tonemappedColor)); // Luma: changed the clamping formula (we could never run LUT extrapolation as these LUTs do TM too)
  lutEncodedColor = lutEncodedColor * (1.0 - (1.0 / 32.0)) + (0.5 / 32.0);

#if COLOR_GRADING_LUT
  float3 lutTonemappedColor = ColorCorrectionTexture.Sample(ColorCorrectionTexture_s, lutEncodedColor).rgb;
#endif
#if FADE_LUT
  float3 fadeColor = ColorCorrectionTextureFade.Sample(ColorCorrectionTextureFade_s, lutEncodedColor).rgb;
#endif

#if COLOR_GRADING_LUT && FADE_LUT
  tonemappedColor = lerp(lutTonemappedColor, fadeColor, Consts[1].w);
#elif COLOR_GRADING_LUT
  tonemappedColor = lutTonemappedColor;
#elif FADE_LUT
  tonemappedColor = fadeColor;
#endif
  }
#if DEVELOPMENT
  else
  {
    if (!skipLUTEncoding)
      tonemappedColor = sqrt_mirrored(tonemappedColor); // Note: this is worse? It's because this was subject to LUT encoding that made everything brighter...
  }
#endif // DEVELOPMENT

#if FILM_GRAIN // We don't allow disabling it for now as it's rare
  float2 grainUV = v1.xy * float2(16,8) + Consts[12].zw;
  // Film grain is purely additive (and subtractive), not focused on shadow neither highlights. This means perceptually it will be a lot more noticeable in shadow.
  float filmGrain = (FilmGrainTexture.Sample(FilmGrainTexture_s, grainUV).x - 0.5) * 0.018;
  postProcessedColor = sqr_mirrored(sqrt_mirrored(postProcessedColor) + filmGrain); // Approximate the same gamma space as "tonemappedColor" would have been for film grain
  tonemappedColor += filmGrain;
#endif
  
  if (!skipLUTEncoding)
    tonemappedColor = sqr_mirrored(tonemappedColor); // Luma: added mirroring (problably not useful if the LUT is UNORM, but it won't hurt). Do not that the original film grain that added negative values around 0 would have flipped back here, causing blacks to raise instead
#endif // COLOR_GRADING_LUT || FADE_LUT || FILM_GRAIN

  outColor.rgb = tonemappedColor;

#if 1 // Luma HDR
  // Restore the color we intentionally clipped away before tonemapping, but with the same altering ratio the TM+Grading applied re-projected on it
  // so we keep any grading on highlights too.
  // The only downside of this is that if ~0.18 was mapped to blue but ~1.0 was mapped to green (e.g.), we clip to ~0.18 so we'd lose the highlights specific grading,
  // but the game doesn't seem to do stuff like that.
  if (doHDR) // TODO: redo SDR too? It was extremely clipped. Also find a way to expand the sat range? Also maybe do a different HDR techniques for other "TONEMAP_TYPE" rather than 3 (which might actually always be used for the whole game)?
  {
    // Note: we could try to do this in BT.2020 but it's unlikely to change much, given that it's the saturation on shadow that would affect it, but we can't control it given it's through the LUT
    tonemapperLostColor = tonemapperInColor != 0.f ? (tonemapperLostColor * (tonemappedColor / tonemapperInColor)) : 0.0;
    outColor.rgb += tonemapperLostColor;
    //outColor.rgb = tonemapperLostColor; // Test raw low color

    // Color grading is massively over saturated and hue shifted in this game, plus it's clipped to BT.709 given it's from a LU;, lowering its intensity and then applying a sat boost after looks better.
    outColor.rgb = lerp(RestoreLuminance(postProcessedColor, outColor.rgb), outColor.rgb, LumaSettings.GameSettings.ColorGradingIntensity);
  }
#endif

#endif // COLOR_GRADING_LUT || FADE_LUT || FILM_GRAIN || TONEMAP_TYPE > 0 || COLOR_MULT

#if USER_BRIGHTNESS_CALIBRATION
  // Likely user brightness levels (defaults to 1 and 0)
  outColor.rgb = outColor.rgb * Consts[12].x + Consts[12].y; // Luma: removed saturate
#endif

#if DEVELOPMENT && 0 // Test: force output scale (useful for exposure testing)
  outColor.rgb *= DVS10 * 10;
#endif

#if COLOR_GRADING_LUT && 0 // Test LUTs
  if (uv.x >= 0.5)
    outColor.rgb = sqr(ColorCorrectionTexture.Sample(ColorCorrectionTexture_s, DVS6).xyz);
  else
    outColor.rgb = sqr(DVS6);
#endif // COLOR_GRADING_LUT

  float tonemappedLuminance = max(GetLuminance(outColor.rgb), 0.0); // Luma: fixed BT.601 luminance, and added <0 safety
  outColor.w = sqrt(tonemappedLuminance); // This is exclusively used by the temporal SMAA and it's squared after
}