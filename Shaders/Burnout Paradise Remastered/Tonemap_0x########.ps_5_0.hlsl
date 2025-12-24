#define LUT_3D 1

#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#if _382FAE1E || _4933D9DA || _7E095B44 || _9613B478 || _A4AAD10C || _BE5A4C0C || _BF6A19BE || _D48AAAA6
#define MOTION_BLUR_QUALITY 2
#elif _259C7CD1 || _36F104E3 || _5A34E415 || _6B63F6E9 || _959BB01D || _A47D890F || _C0305DC5 || _D29A0825
#define MOTION_BLUR_QUALITY 1
#endif

#if _10807557 || _382FAE1E || _4933D9DA || _57B58AF1 || _6B63F6E9 || _959BB01D || _B9F09845 || _BE5A4C0C || _BF6A19BE || _C0305DC5 || _D29A0825 || _D772072E
#define ENABLE_DOF 1
#endif

#if _259C7CD1 || _5A34E415 || _6B63F6E9 || _959BB01D || _A4AAD10C || _B9F09845 || _BE5A4C0C || _BF6A19BE || _C0BD8148 || _D48AAAA6 || _D772072E || _FDE602F4
#define ENABLE_SSAO 1
#endif

#ifndef ENABLE_COLOR_GRADING_LUT // Can be overriden by users
#if _4933D9DA || _57B58AF1 || _5A34E415 || _7E095B44 || _959BB01D || _A47D890F || _BF6A19BE || _D29A0825 || _D48AAAA6 || _D772072E || _DD905507 || _FDE602F4
#define ENABLE_COLOR_GRADING_LUT 1
#endif
#endif

// Default settings (all off), given that we only specify which permutations use each feature.
// There's 24 combinations of all of these.
// Permutation "07297021" is void of all optional features.
#ifndef MOTION_BLUR_QUALITY
// 0: Disabled
// 1: Medium
// 2: High
#define MOTION_BLUR_QUALITY 0
#endif
#ifndef ENABLE_COLOR_GRADING_LUT
#define ENABLE_COLOR_GRADING_LUT 0
#endif
#ifndef ENABLE_DOF
#define ENABLE_DOF 0
#endif
#ifndef ENABLE_SSAO
#define ENABLE_SSAO 0
#endif

cbuffer _Globals : register(b0)
{
  float4 GlobalParams : packoffset(c0);
  float4 DofParamsA : packoffset(c1);
  float4 DofParamsB : packoffset(c2);
  float4 BloomColour : packoffset(c3);
  float4 VignetteInnerRgbPlusMul : packoffset(c4);
  float4 VignetteOuterRgbPlusAdd : packoffset(c5);
  float4 Tint2dColour : packoffset(c6);
  float4 BlurMatrixZZZ : packoffset(c7);
  float4 MotionBlurStencilValues : packoffset(c8);
  float4 AdaptiveLuminanceValues : packoffset(c9);
}

#if ENABLE_IMPROVED_MOTION_BLUR
cbuffer _VSGlobals : register(b1)
{
  float4 VignetteCentreXyScaleXy : packoffset(c0);
  float4 VignetteAngle : packoffset(c1);
  float4 BlurMatrixXXX : packoffset(c2);
  float4 BlurMatrixYYY : packoffset(c3);
  float4 BlurMatrixWWW : packoffset(c4);
}

// Luma adds the previous frame parameters too, to better blend (smooth) motion blur
cbuffer _PrevGlobals : register(b2)
{
  float4 PrevGlobalParams : packoffset(c0);
  float4 PrevDofParamsA : packoffset(c1);
  float4 PrevDofParamsB : packoffset(c2);
  float4 PrevBloomColour : packoffset(c3);
  float4 PrevVignetteInnerRgbPlusMul : packoffset(c4);
  float4 PrevVignetteOuterRgbPlusAdd : packoffset(c5);
  float4 PrevTint2dColour : packoffset(c6);
  float4 PrevBlurMatrixZZZ : packoffset(c7);
  float4 PrevMotionBlurStencilValues : packoffset(c8);
  float4 PrevAdaptiveLuminanceValues : packoffset(c9);
}
cbuffer _PrevVSGlobals : register(b3)
{
  float4 PrevVignetteCentreXyScaleXy : packoffset(c0);
  float4 PrevVignetteAngle : packoffset(c1);
  float4 PrevBlurMatrixXXX : packoffset(c2);
  float4 PrevBlurMatrixYYY : packoffset(c3);
  float4 PrevBlurMatrixWWW : packoffset(c4);
}
#endif

SamplerState SamplerSource_s : register(s0); // Linear sampler
SamplerState SamplerBloom_s : register(s1); // Linear sampler
SamplerState SamplerDof_s : register(s2); // Linear sampler
SamplerState Sampler3dTint_s : register(s3); // Linear sampler
SamplerState SamplerDepth_s : register(s4); // Point sampler
SamplerState SamplerSSAO_s : register(s6); // Linear sampler
SamplerState SamplerParticles_s : register(s7); // Linear sampler
SamplerState samplerLastAvgLuminance_s : register(s9); // Linear sampler

Texture2D<float4> SamplerSourceTexture : register(t0); // Full res
Texture2D<float4> SamplerBloomTexture : register(t1); // Quarter res
Texture2D<float4> SamplerDofTexture : register(t2); // Half res
Texture3D<float4> Sampler3dTintTexture : register(t3); // 32x32x32
Texture2D<float> SamplerDepthTexture : register(t4); // Half res (downscaled with max of 4 texels)
Texture2D<float> SamplerSSAOTexture : register(t6); // Half res
Texture2D<float4> SamplerParticlesTexture : register(t7); // Half res
Texture2D<float2> samplerLastAvgLuminanceTexture : register(t9); // 1x1

static const float MotionBlurStrencilThreshold = 0.7;
#if ENABLE_IMPROVED_MOTION_BLUR && MOTION_BLUR_BLUR_DISTANT_CARS // Luma: avoid motion blur not applying to cars in the distance
static const float LumaForcedMotionBlurDepth = 0.994; // Values lower than this would apply MB onto the player car too, and to other cars that are too close (it's good to see them crisply when they are close!) // TODO: scale this with or something "average(abs(BlurMatrixZZZ.xy))", so it's affected by the player speed
#else // Vanilla like
static const float LumaForcedMotionBlurDepth = 1.0;
#endif

#if ENABLE_IMPROVED_MOTION_BLUR
float2 CalculateMotionBlurUVOffset(float2 uv, float depth)
{
  float3 temp = BlurMatrixZZZ.xyz * depth + (BlurMatrixXXX.xyz * uv.x + BlurMatrixYYY.xyz * uv.y + BlurMatrixWWW.xyz);
  return (temp.xy + temp.z * uv) * max(MotionBlurStencilValues.y, MotionBlurStencilValues.x);
}
#endif

float3 ApplyLUT(float3 color, float3 sdrColor, Texture3D<float4> _texture, SamplerState _sampler)
{
  float vanillaCompressionRatio = LumaSettings.GameSettings.OriginalTonemapperColorIntensity <= 1.0 ? max(max3(sdrColor), 1.0) : 1.0; // Alternative

  bool lutExtrapolation = true;
  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = color;
  extrapolationData.vanillaInputColor = sdrColor / vanillaCompressionRatio; // Nicely compress back to 1 to fit the LUT
  
  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
  extrapolationSettings.lutSize = 0; // 32... it probably doesn't ever change but let it be automatically determined
#if DEVELOPMENT && 0 // Seems like there's no bad hue shifts in the game (we now have "LumaSettings.GameSettings.OriginalTonemapperColorIntensity" anyway)
  extrapolationSettings.enableExtrapolation = DVS1 <= 0.0;
#endif
  extrapolationSettings.inputLinear = true;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = false;
  extrapolationSettings.outputLinear = true;
  if (LumaSettings.GameSettings.OriginalTonemapperColorIntensity < 0.0) // Extra (hidden?) setting. Doesn't really match vanilla, but it's closer to it!
  {
    extrapolationSettings.clipExtrapolationToWhite = true;
  }
  else
  {
    extrapolationSettings.vanillaLUTRestorationAmount = saturate(LumaSettings.GameSettings.OriginalTonemapperColorIntensity); // Not a 100% match!
  }
#if 1 // High quality. Not particularly needed in this game as most LUTs are neutral, but it won't hurt.
  extrapolationSettings.extrapolationQuality = 2;
#endif
  
  color = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);

  if (LumaSettings.GameSettings.OriginalTonemapperColorIntensity >= 0)
  {
    color *= lerp(1.0, vanillaCompressionRatio, saturate(LumaSettings.GameSettings.OriginalTonemapperColorIntensity));
  }

  return color;
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
#if MOTION_BLUR_QUALITY > 0
  float3 v2 : TEXCOORD1,
#endif
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;

  bool forceVanilla = ShouldForceSDR(v1.xy);

  float3 bloomTextureColor = SamplerBloomTexture.Sample(SamplerBloom_s, v1.xy).xyz;

  float invAmbientOcclusion = 1.0;
  float paritclesAlpha = 0.0;
  float skyAlpha = 0.0;
#if ENABLE_SSAO
  invAmbientOcclusion = SamplerSSAOTexture.Sample(SamplerSSAO_s, v1.xy).x;
  paritclesAlpha = SamplerParticlesTexture.Sample(SamplerParticles_s, v1.xy).w;
#if 1 // Luma: make sure particles alpha isn't beyond range due to texture upgrades
  paritclesAlpha = saturate(paritclesAlpha);
#endif
#endif

  float3 dofTextureColor = 0.0;
  float depth = 0.0;
#if ENABLE_DOF || MOTION_BLUR_QUALITY > 0
  dofTextureColor = SamplerDofTexture.Sample(SamplerDof_s, v1.xy).xyz;
  depth = SamplerDepthTexture.Sample(SamplerDepth_s, v1.xy).x; // This will use nearest sampling, which will take the max of 4 samples from the original higher res (the game does that, the min might have been better for MB)
#endif

  if (forceVanilla)
  {
    bloomTextureColor = saturate(bloomTextureColor);
    dofTextureColor = saturate(dofTextureColor);
  }

  // Motion Blur
  // Note that if the effect is boosted, when going fast, some frames it warps backwards, looking weird,
  // they probably never noticed as the effect is fairly weak in the game.
  // This motion blur can only properly work when the camera is moving forwards, otherwise it'd look weird due to the sampling offset direction converging in the center.
#if MOTION_BLUR_QUALITY > 0

#if MOTION_BLUR_QUALITY >= 2
  const int originalIterations = 16;
#else // MOTION_BLUR_QUALITY <= 1
  const int originalIterations = 4;
#endif // MOTION_BLUR_QUALITY >= 2
  int iterations = originalIterations;
#if ENABLE_IMPROVED_MOTION_BLUR
  iterations = forceVanilla ? originalIterations : 48; // 32 looks good but 48 seems like the sweet spot // TODO: make this scale by velocity to improve performance?
#endif

  float4 baseSceneColor = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy); // This is a mask that represents different types of objects (like a stencil)
  // Flag to ignore the player car (and other cars too...) (their alpha is 0 and so is their MB intensity, usually). Ideally the alpha would be the forward velocity compared to the camera, but it seems to just be a stencil mask.
  float motionBlurStencilValue = (baseSceneColor.a < MotionBlurStrencilThreshold) ? lerp(MotionBlurStencilValues.y, MotionBlurStencilValues.x, saturate(InverseLerp(LumaForcedMotionBlurDepth, 1.0, depth))) : MotionBlurStencilValues.x;
  float targetAspectRatio = LumaSettings.GameSettings.InvRenderRes.y / LumaSettings.GameSettings.InvRenderRes.x;

  float mbLumaMultiplier = LumaSettings.GameSettings.MotionBlurIntensity;
  float4 currentBlurMatrixZZZ = BlurMatrixZZZ;
#if ENABLE_IMPROVED_MOTION_BLUR
  if (!forceVanilla)
  {
    float4 currentBlurMatrixXXX = BlurMatrixXXX;
    float4 currentBlurMatrixYYY = BlurMatrixYYY;
    float4 currentBlurMatrixWWW = BlurMatrixWWW;
    
#if SMOOTH_MOTION_BLUR // Smooth over with the previous frame (at 50%, independently of the frame rate, given that this game always runs at 60 fps), to avoid weird jitters in MB offsets. This isn't perfect, but it seems to help a bit (though it lags MB a bit too when turning!)
    currentBlurMatrixXXX = lerp(currentBlurMatrixXXX, PrevBlurMatrixXXX, 0.5);
    currentBlurMatrixYYY = lerp(currentBlurMatrixYYY, PrevBlurMatrixYYY, 0.5);
    currentBlurMatrixZZZ = lerp(currentBlurMatrixZZZ, PrevBlurMatrixZZZ, 0.5);
    currentBlurMatrixWWW = lerp(currentBlurMatrixWWW, PrevBlurMatrixWWW, 0.5);
#endif

#if 0 // Example of MB param values (dumped from GPU). These prove that flickering at very high speeds is from parameters and not from anything else.
    currentBlurMatrixXXX.xyz = float3(-0.246477,-3.16045e-05, -3.64145e-07);
    currentBlurMatrixYYY.xyz = float3(5.60115e-05, -0.246424, -0.000106995);
    currentBlurMatrixWWW.xyz = float3(0.122511, 0.129377, 0.0);
    currentBlurMatrixZZZ.xyz = -float3(0.122511, 0.129377, -0.246477);
#endif

#if FORWARDS_ONLY_MOTION_BLUR
    float xyAverage = average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y));
    currentBlurMatrixXXX.x = xyAverage;
    currentBlurMatrixXXX.yz = 0.0;
    currentBlurMatrixYYY.y = xyAverage;
    currentBlurMatrixYYY.xz = 0.0;
    currentBlurMatrixWWW.xy = average(currentBlurMatrixWWW.xy);
    currentBlurMatrixZZZ.xyz = -float3(currentBlurMatrixWWW.xy, xyAverage);
#endif

#if REDUCE_HORIZONTAL_MOTION_BLUR // Reduce the MB intensity multiplier when there's horizontal camera movement, or it'd get too blurry, especially at high intensities
    float horizontalMBShiftIntensity = max(max(abs(currentBlurMatrixXXX.y), abs(currentBlurMatrixXXX.z)), max(abs(currentBlurMatrixYYY.x), abs(currentBlurMatrixYYY.z)));
    float forwardsMBShiftIntensity = max(abs(currentBlurMatrixXXX.x), abs(currentBlurMatrixYYY.y));
    mbLumaMultiplier = lerp(mbLumaMultiplier, 1.0, saturate(horizontalMBShiftIntensity * 10.0 / forwardsMBShiftIntensity)); // Empyrically found scaling
#elif 0 // Failed attempt at exclusively scaling the forwards velocity, instead of reducing the blur multiplier when there's camera movement too (which is a workaround for being unable to fix the math). This doesn't scale it mirrored around the center of the screen.
    if (DVS1)
    {
    // currentBlurMatrixXXX.x -= 0.5;
    // currentBlurMatrixYYY.y -= 0.5;
    // currentBlurMatrixZZZ.xyz -= 0.5;
    // currentBlurMatrixWWW.xy -= 0.5;

    // currentBlurMatrixXXX.x *= mbLumaMultiplier;
    // currentBlurMatrixYYY.y *= mbLumaMultiplier;
    // currentBlurMatrixZZZ.z *= mbLumaMultiplier;
    currentBlurMatrixZZZ.xy *= mbLumaMultiplier;
    currentBlurMatrixWWW.xy *= mbLumaMultiplier;
    mbLumaMultiplier = 1.0;
    
    // currentBlurMatrixXXX.x += 0.5;
    // currentBlurMatrixYYY.y += 0.5;
    // currentBlurMatrixZZZ.xyz += 0.5;
    // currentBlurMatrixWWW.xy += 0.5;
    }
#elif 0 // Another failed attempt
    float averageDepth = 0.1; // Made up common value
#if 0 // Only apply the luma MB multiplier if bloom is mostly going forwards, to emulate PS2 bloom, not when the camera goes left or right
    float2 centralUV = 0.5;
    float2 centralMB = CalculateMotionBlurUVOffset(centralUV, averageDepth);
    mbLumaMultiplier = lerp(mbLumaMultiplier, 1.0, saturate((average(centralMB) * 1025.0 * DVS10))); // Uncalibrated
#elif 0 // This doesn't seem to be reliable, it flickers more than expected, not sure why
    float2 rightMB = CalculateMotionBlurUVOffset(float2(0.75, 0.5), averageDepth);
    float2 leftMB = CalculateMotionBlurUVOffset(float2(0.25, 0.5), averageDepth);
    bool sameDirection = rightMB.x >= 0.0 == leftMB.y >= 0.0;
    if (sameDirection)
      mbLumaMultiplier = 1.0;
#if 0
    if (sameDirection)
    {
      o0 = rightMB.x >= 0.0 ? 1 : 0;
      return;
    }
#endif
#endif
#endif

    // Re-create the values the vertex buffer would have generated to be able to do advanced math on them.
    // Weird enough MB is generated from UV values, so the math is needlessly unintuitive because it's not mirrored around zero (NDC).
    v2.xyz = currentBlurMatrixXXX.xyz * v1.x + currentBlurMatrixYYY.xyz * v1.y + currentBlurMatrixWWW.xyz;

#if 0 // Force MB to be from the center (actually this removes it all, due to missing x.z and y.z)
    v2.xyz = float3(average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)), 0, 0) * v1.x + float3(0, average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)), 0) * v1.y + float3(average(currentBlurMatrixWWW.xy), average(currentBlurMatrixWWW.yx), currentBlurMatrixWWW.z);
    currentBlurMatrixZZZ.xyz = -float3(average(currentBlurMatrixWWW.xy), average(currentBlurMatrixWWW.yx), average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)));
#elif 0
    //float zMidPoint = average(float2(currentBlurMatrixXXX.z, currentBlurMatrixYYY.z));
    //currentBlurMatrixXXX.z = lerp(zMidPoint, currentBlurMatrixXXX.z, 1.0 / targetAspectRatio);
    //currentBlurMatrixYYY.z = lerp(zMidPoint, currentBlurMatrixYYY.z, 1.0 / targetAspectRatio);

    //currentBlurMatrixXXX.z /= targetAspectRatio;
    //currentBlurMatrixYYY.z /= targetAspectRatio;
    
    // Removing the near zero offsets from the matrices breaks bloom when the car or camera are turning. This means that these values influence how bloom is shifted from the center.
    //v2.xyz = float3(currentBlurMatrixXXX.x, 0, 0) * v1.x + float3(0, currentBlurMatrixYYY.y, 0) * v1.y + currentBlurMatrixWWW.xyz;
    //v2.xyz = float3(currentBlurMatrixXXX.x, currentBlurMatrixXXX.y, 0) * v1.x + float3(currentBlurMatrixYYY.x, currentBlurMatrixYYY.y, 0) * v1.y + currentBlurMatrixWWW.xyz;
    //v2.xyz = float3(currentBlurMatrixXXX.x, 0, currentBlurMatrixXXX.z) * v1.x + float3(0, currentBlurMatrixYYY.y, currentBlurMatrixYYY.z) * v1.y + currentBlurMatrixWWW.xyz;
    //v2.xyz = float3(currentBlurMatrixXXX.x, currentBlurMatrixXXX.y * DVS2, currentBlurMatrixXXX.z * DVS3) * v1.x + float3(currentBlurMatrixYYY.x * DVS2, currentBlurMatrixYYY.y, currentBlurMatrixYYY.z * DVS3) * v1.y + currentBlurMatrixWWW.xyz;

    // The only way to have scalable horizontal (fullscreen, camera rotation based) motion blur. This keeps forwards MB only
    v2.xyz = float3(average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)), currentBlurMatrixXXX.y * DVS2, currentBlurMatrixXXX.z * DVS3) * v1.x + float3(currentBlurMatrixYYY.x * DVS2, average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)), currentBlurMatrixYYY.z * DVS3) * v1.y + float3(average(currentBlurMatrixWWW.xy), average(currentBlurMatrixWWW.yx), currentBlurMatrixWWW.z);
    currentBlurMatrixZZZ.xyz = -float3(average(currentBlurMatrixWWW.xy), average(currentBlurMatrixWWW.yx), average(float2(currentBlurMatrixXXX.x, currentBlurMatrixYYY.y)));
#endif
  }
#endif // ENABLE_IMPROVED_MOTION_BLUR
  // "BlurMatrixZZZ.xy" is identical but negative, when compared to "BlurMatrixWWW.xy" from the vertex shader, and "BlurMatrixZZZ.z" is equal to "BlurMatrixXXX.x" and "BlurMatrixXXX.y" (but negative) (which one exactly? Maybe their average?).
  // "BlurMatrixZZZ" and "BlurMatrixWWW" are directy proportional to the camera movement in its forward axis (and flipped when going backwards).
  // "BlurMatrixXXX.z" and "BlurMatrixYYY.z" control the diagonal blur (it seems to be a bit different depending on the aspect ratio).
  // "BlurMatrixXXX.y" and "BlurMatrixYYY.x" control the vertical blur.
  // "BlurMatrixXXX.x" and "BlurMatrixYYY.y" control the frontal (forwards) blur.
  // Theoretically this means that at depth 1, all motion blur from the vertex shader is removed, as the final sum is ~0, at least when driving fowards.
  // All of these values are completely independent from the aspect ratio or resolution, motion blur simply blur things towards each edge of the screen (in that direction).
  // TODO: It seems like somehow, even if samplers are "clamp", strong MB uv offsets can wrap around and sample the other side of the image.
  float2 mbOffset = (currentBlurMatrixZZZ.xy * depth + v2.xy) + ((currentBlurMatrixZZZ.z * depth + v2.z) * v1.xy);
#if 0 // Test: if I understood correctly, this should be a decent approximation of the MB, with less code (not working properly yet)
  mbOffset = (currentBlurMatrixZZZ.xy * (1.0 - depth)) - (currentBlurMatrixZZZ.y * (1.0 - depth) * v1.xy);
#endif
  mbOffset *= motionBlurStencilValue * mbLumaMultiplier;

#if 0 // Luma: motion blur was almost invisible at 32:9 due to the horizontal offset not being scaled by the aspect ratio (like the fov would be). Disabled as this is actually wrong, somehow it breaks MB, warping stuff at the edges... Maybe UW was already accounted for.
#if ENABLE_IMPROVED_MOTION_BLUR // MB actually seems to have identical offset values for x and y so ideally we should scale it from a 1:1 aspect ratio, and anyway it was too faint in this game so this is ok
  // Emulate 16:9 (or well, a 1:1 screen, given that MB doesn't ever acknowledge aspect ratio and has the same values for X and Y)
  mbOffset.x /= forceVanilla ? 1.0 : targetAspectRatio;
#else // !ENABLE_IMPROVED_MOTION_BLUR
  mbOffset.x /= forceVanilla ? 1.0 : max(targetAspectRatio / (16.0 / 9.0), 1.0); // 4:3 was probably ok already
#endif // ENABLE_IMPROVED_MOTION_BLUR
#endif

#if 0 // Test
  o0 = 0;
  o0.xy = max(mbOffset, 0.0) * 100;
  //o0.xy = abs(centralMB) * 100.01;
  return;
#elif 0 // Visualize blur offsets
  o0.rgb = BlurMatrixZZZ.xyz * motionBlurStencilValue * LumaSettings.GameSettings.MotionBlurIntensity;
  o0.rg *= 10;
  o0.rgb = abs(o0.rgb);
  return;
#endif

  float2 motionBlurUV = v1.xy;
  float2 noise = frac(v0.x * 0.618034 + v0.y * v0.y * 0.381966); // Apply some noise pattern to make it look nicer. This barely does anything, especially as the sample count goes up.
#if ENABLE_IMPROVED_MOTION_BLUR // Disable noise (set it to its neutral value, the average of the original value, to avoid offsetting the blur amount), it seems to only be detrimental with the improved MB
  noise = 0.5;
#endif
  mbOffset /= float(iterations);
  motionBlurUV += mbOffset * noise; // Apply the noise only in the first iteration, as a 0-1 multiplier, to offsetted the blur by one sampling "range" back and forth and thus cover the whole area
  float3 tempSceneColorSum = 0.0;
  int i = 0;
  int validIterations = 0;
#if TEST_SDR_HDR_SPLIT_VIEW_MODE // Avoids a warning due to dynamic iterations number
  [loop]
#endif
  while (i < iterations)
  {
    float2 localMotionBlurUV = motionBlurUV;
#if DEVELOPMENT && 0 // Tried changing the intensity with a pow from the neutral UV but it lLooks bad
    localMotionBlurUV -= v1.xy;
    localMotionBlurUV = pow(abs(localMotionBlurUV), DVS9) * sign(localMotionBlurUV);
    localMotionBlurUV += v1.xy;
#endif

    float localInvAmbientOcclusion = invAmbientOcclusion;
    float localParitclesAlpha = paritclesAlpha;
#if ENABLE_SSAO && ENABLE_IMPROVED_MOTION_BLUR // Luma: particles here weren't sampled in the right place for bloom (this is pretty heavy)
    if (!forceVanilla)
    {
      localInvAmbientOcclusion = SamplerSSAOTexture.Sample(SamplerSource_s, localMotionBlurUV).x;
      localParitclesAlpha = SamplerParticlesTexture.Sample(SamplerSource_s, localMotionBlurUV).w;
      localParitclesAlpha = saturate(paritclesAlpha);
    }
#endif
    float4 tempSceneTextureColor = SamplerSourceTexture.Sample(SamplerSource_s, localMotionBlurUV);
    float localMotionBlurStencilValue = (tempSceneTextureColor.a < MotionBlurStrencilThreshold) ? lerp(MotionBlurStencilValues.y, MotionBlurStencilValues.x, saturate(InverseLerp(LumaForcedMotionBlurDepth, 1.0, depth))) : MotionBlurStencilValues.x;
#if ENABLE_IMPROVED_MOTION_BLUR && MOTION_BLUR_IMPROVE_STENCIL_FILTER // Avoids motion blur next to cars leaking cars pixels (this results in some slightly weird outlines around cars, but it still looks better than broken bloom around objects, maybe the new error is due to linear sampling leaking values? Probably because many textures are half res actually)
    bool ignoreMB = localMotionBlurStencilValue < motionBlurStencilValue;
    if (!ignoreMB)
#endif
    {
      if (forceVanilla)
      {
        tempSceneTextureColor.rgb = saturate(tempSceneTextureColor.rgb);
      }
      else
      {
        tempSceneTextureColor.rgb = max(tempSceneTextureColor.rgb, -FLT16_MAX); // Luma: NaNs protection
      }
#if 0 // Test: Force draw only the last bloom iteration (the most offsetted one)
      if (i == iterations - 1)
      {
        tempSceneColorSum = 0.0;
        validIterations = 0.0;
      }
#endif
      tempSceneColorSum += lerp(tempSceneTextureColor.rgb * localInvAmbientOcclusion, tempSceneTextureColor.rgb, localParitclesAlpha); // Apply SSAO if not on particles


      // The sky (possibly) has the alpha set to 2 to skip doing fake HDR twice
      // Note that we could actually check if the depth is 1 too
      skyAlpha += tempSceneTextureColor.a >= 1.5 ? 1.0 : 0.0;

      validIterations++;
    }
    
    motionBlurUV += mbOffset;

    i++;
  }
#if !ENABLE_IMPROVED_MOTION_BLUR
  validIterations = iterations;
#endif
  float3 composedColor = tempSceneColorSum / float(validIterations);
  skyAlpha /= float(validIterations);

#if ENABLE_IMPROVED_MOTION_BLUR
  if (validIterations == 0)
  {
    composedColor = baseSceneColor.rgb;
    if (forceVanilla)
    {
      composedColor = saturate(composedColor);
    }
    else
    {
      composedColor = max(composedColor, -FLT16_MAX); // Luma: NaNs protection
    }
    composedColor = lerp(composedColor * invAmbientOcclusion, composedColor, paritclesAlpha); // Apply SSAO if not on particles
    
    skyAlpha = baseSceneColor.a >= 1.5 ? 1.0 : 0.0;
  }
#endif
  
#else // MOTION_BLUR_QUALITY <= 0

  float4 tempSceneColor = SamplerSourceTexture.Sample(SamplerSource_s, v1.xy);
  float3 composedColor = tempSceneColor.rgb;
  if (forceVanilla)
  {
    composedColor = saturate(composedColor);
  }
  else
  {
    composedColor = max(composedColor, -FLT16_MAX); // Luma: NaNs protection
  }
  composedColor = lerp(composedColor * invAmbientOcclusion, composedColor, paritclesAlpha); // Apply SSAO if not on particles
  
  skyAlpha = tempSceneColor.a >= 1.5 ? 1.0 : 0.0;

#endif // MOTION_BLUR_QUALITY > 0

#if ENABLE_DOF // Note: this creates some steps at the edges between sky and mountains (etc), likely due to most textures being half res, and dof being limited by its design the first place
  // Depth of Field
  float dofAlpha = DofParamsB.x * saturate(max(DofParamsB.y * (DofParamsA.y - depth), DofParamsB.z * (-DofParamsA.z + depth)));
  composedColor = lerp(composedColor, dofTextureColor.xyz, dofAlpha); // Blend in DoF by distance etc (focal plane)
#endif // ENABLE_DOF

  // Scene exposure (hardcoded by time of day etc), used to keep visibility balanced
  // Note: bloom isn't affected by this so if this reduces the scene color, in contrast bloom will be stronger.
  composedColor *= GlobalParams.x;

#if 1 // This is some kind of auto exposure loop, based on the luminance of the previous frame (pre tonemapping)
  r2.xy = samplerLastAvgLuminanceTexture.Sample(samplerLastAvgLuminance_s, float2(0.5, 0.5)).xy; // TODO: test if HDR messes up the balance of this? It doesn't seem to at first glange. We could saturate the luminance generation otherwise, or here.
  r0.w = r2.x - r2.y;
  r1.w = (abs(r0.w) < AdaptiveLuminanceValues.w);
  r0.w = r1.w ? 0 : r0.w;
  r1.w = AdaptiveLuminanceValues.z - AdaptiveLuminanceValues.y;
  r0.w = r0.w / r1.w;
  r0.w = max(-1, r0.w);
  r0.w = min(1, r0.w);
  r0.w = AdaptiveLuminanceValues.x * r0.w;
  r1.w = r0.w * r0.w;
  r0.w = r1.w * r0.w;
  composedColor += r0.w;
#endif

  float3 sdrComposedColor = composedColor;

  // Bloom
  float3 finalBloomColor = bloomTextureColor * BloomColour.xyz * LumaSettings.GameSettings.BloomIntensity;
#if 1 // Luma: fixed bloom going negative with HDR values
  if (!forceVanilla)
  {
    bool improvedBloom = false;
#if ENABLE_IMPROVED_BLOOM
    improvedBloom = LumaSettings.DisplayMode == 1;
#endif // ENABLE_IMPROVED_BLOOM
    if (improvedBloom) // Just add the bloom as it came, it looks nicer in HDR
    {
      //composedColor = RestoreLuminance(finalBloomColor + composedColor, composedColor, true); // Alternative version, but blending bloom with fixed luminance doesn't work...
      composedColor += finalBloomColor;
    }
    else // Almost the same as the vanilla formula, but re-written to not for colors beyond 1
    {
      composedColor += finalBloomColor * (1.0 - saturate(composedColor));
    }
  }
  else
  {
    composedColor += finalBloomColor - saturate(finalBloomColor * composedColor);
  }
#else
  // This avoids adding bloom on highlights, preventing the image from clipping too much.
  composedColor += finalBloomColor - saturate(finalBloomColor * composedColor);
#endif
  // Note: theoretically this isn't the original bloom color, but it's actually slightly "better"
  sdrComposedColor += finalBloomColor * (1.0 - saturate(sdrComposedColor));

  // Color grading LUT
#if ENABLE_COLOR_GRADING_LUT
#if 1 // Luma
  if (!forceVanilla)
  {
    float clippedAmount = 0.5 / 32.0; // The first and last half texels of the LUT were clipped away (in gamma space)

    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
    sdrComposedColor = ((sdrComposedColor - 0.5) * (1.0 + clippedAmount)) + 0.5; // Emulate the SDR LUT error with LUT extrapolation
    sdrComposedColor = gamma_to_linear(sdrComposedColor, GCT_MIRROR);

    // TODO: put this in a formula given that it's identical to BS2
    // LUTs were clipped around the first and half texel due to bad sampling math. This will emulate the shadow darkening and highlight brightening from it (contrast boost), without causing clipping.
    // The original error applied in gamma space but we do it in linear.
    // This doesn't look too good with the HDR boost, as it creates too much contrast, making it look like AutoHDR, and causing issues in near black visibility, due to the sky being so bright. Game looked too filmic anyway.
#if LUT_SAMPLING_ERROR_EMULATION_MODE > 0
    float3 previousColor = composedColor.rgb;
    
    // Adjust params for shadows
    // Empyrically found values that look good
    float adjustmentScale = 0.15; // Basically the added contrast curve strength
    float adjustmentRange = 1.0 / 3.0; // Theoretically the added shadow crush would have happened until 0.5 (in gamma space, and ~0.218 in linear), by then it would have faded out (and highlights clipping would have begun, but we don't simulate that)
    float adjustmentPow = 1.0; // Values > 1 might look good too, this kinda controls the "smoothness" of the contrast curve
#if LUT_SAMPLING_ERROR_EMULATION_MODE != 2 // Per channel (it looks nicer)
    composedColor.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma(previousColor, GCT_POSITIVE) / adjustmentRange, adjustmentPow)));
#else // LUT_SAMPLING_ERROR_EMULATION_MODE == 2 // By luminance
    composedColor.rgb *= lerp(adjustmentScale, 1.0, saturate(pow(linear_to_gamma1(max(GetLuminance(previousColor), 0.0)) / adjustmentRange, adjustmentPow)));
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 2

#if LUT_SAMPLING_ERROR_EMULATION_MODE != 3
    // Adjust params for highlights
    adjustmentScale = 1.0 - adjustmentScale; // Flip it because it looks good like this
    adjustmentRange = 1.0 - adjustmentRange; // Do the remaining range
    float3 highlightsPerChannelScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma(previousColor, GCT_SATURATE)) / adjustmentRange, adjustmentPow)));
    float highlightsByLuminanceScale = lerp(1.0 / adjustmentScale, 1.0, saturate(pow((1.0 - linear_to_gamma1(saturate(GetLuminance(previousColor)))) / adjustmentRange, adjustmentPow)));
#if 0 // Per channel (looks deep fried)
    composedColor.rgb *= highlightsPerChannelScale;
#elif 0 // By luminance (looks like AutoHDR)
    composedColor.rgb *= highlightsByLuminanceScale;
#else // Mixed (looks best on highlights)
    composedColor.rgb *= lerp(highlightsPerChannelScale, highlightsByLuminanceScale, LumaSettings.DisplayMode == 1 ? 0.75 : 0.333);
#endif
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE != 3
#endif // LUT_SAMPLING_ERROR_EMULATION_MODE > 0

    float3 preLUTColor = composedColor;
    composedColor = ApplyLUT(composedColor, sdrComposedColor, Sampler3dTintTexture, Sampler3dTint_s); // Output is linear

    // TODO: turn this into a function given it's used by Mafia III as well? We could also try to get the neutral LUT color at each location and remove the filter it applied by calculating the rgb ratio of the lutted color against the luminance or something
    // Most of the blue is from vignette, not LUTs, these just add some contrast etc
    float3 lutMidGreyGamma = Sampler3dTintTexture.Sample(Sampler3dTint_s, 0.5).rgb;
    float3 lutMidGreyLinear = gamma_to_linear(lutMidGreyGamma, GCT_NONE); // Turn linear
    float lutMidGreyBrightnessLinear = max(GetLuminance(lutMidGreyLinear), 0.0); // Normalize it by luminance
    float blueCorrectionIntensity = LumaSettings.GameSettings.ColorGradingFilterReductionIntensity; // Note that this will correct other color filters as well!
    composedColor /= (lutMidGreyLinear != 0.0) ? lerp(1.0, safeDivision(lutMidGreyLinear, lutMidGreyBrightnessLinear, 1), blueCorrectionIntensity) : 1.0;

    composedColor = lerp(preLUTColor, composedColor, LumaSettings.GameSettings.ColorGradingIntensity);

    composedColor = linear_to_gamma(composedColor, GCT_MIRROR);
  }
  else
  {
    composedColor = Sampler3dTintTexture.Sample(Sampler3dTint_s, composedColor).xyz;
  }
#else // The original LUT sampling failed to acknowledge the half texel offset of LUTs and clipped colors
  composedColor = Sampler3dTintTexture.Sample(Sampler3dTint_s, composedColor).xyz;
#endif
#endif // ENABLE_COLOR_GRADING_LUT
  
  // Luma: HDR boost
  // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that. Do it before bloom to avoid bloom going crazy too
  if (!forceVanilla && LumaSettings.DisplayMode == 1)
  {
    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);

    float fakeHDRScale = 1.0;
#if 0 // This just creates too many edges, we can't really do it properly, we just do HDR boost twice on the sky
    fakeHDRScale = 1.0 - skyAlpha;
#endif

    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = LumaSettings.GameSettings.HDRBoostIntensity * 0.1125 * fakeHDRScale; // 0.1-0.15 looks good in most places. 0.2 looks better in dim scenes, but is too much AutoHDR like in bright scenes
    float fakeHDRSaturation = 0.5;
    composedColor = FakeHDR(composedColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation);
    
    composedColor.xyz = linear_to_gamma(composedColor, GCT_MIRROR);
  }

#if ENABLE_VIGNETTE
  // Vignette (note that this is also affected by the user brightness calibration, acting as contrast modulator)
  // This is partially what added the blue tint too.
  float vignetteIntensity = saturate(VignetteOuterRgbPlusAdd.w + sqrt(dot(v1.zw, v1.zw))); // Calculate vignette based on the distance from the center of the screen. This seems to be UW friendly.
  vignetteIntensity = (vignetteIntensity * -2 + 3) * vignetteIntensity * vignetteIntensity;
  float3 vignetteInnerRgbPlusMul = VignetteInnerRgbPlusMul.xyz;
  float3 vignetteOuterRgbPlusAdd = VignetteOuterRgbPlusAdd.xyz;

#if 1 // Luma
  float vignetteColorIntensity = 1.0;
#if 1
  // Remove the color filter of the central vignette from the edges vignette,
  // this is because the center has some blue tint too, and that's "excessive" but the one at the edges is kinda done to emulate the sky being blue, and that we want to keep.
  // We leave the upper part (sky) blue, this makes sense in most shots as the camera is always behind the car.
  // Note that when pausing the game, vignette values go to 0 (to darken the background to full black and then fade it back out to the normal vignete color), and if grading is set to 0%, that won't happen.
  if (v1.w < 0.5)
  {
    float3 vignetteCenterColorRatio = safeDivision(GetLuminance(vignetteInnerRgbPlusMul), vignetteInnerRgbPlusMul, 1);
    vignetteInnerRgbPlusMul *= lerp(1.0, vignetteCenterColorRatio, LumaSettings.GameSettings.ColorGradingFilterReductionIntensity);
    vignetteOuterRgbPlusAdd *= lerp(1.0, vignetteCenterColorRatio, LumaSettings.GameSettings.ColorGradingFilterReductionIntensity);
    vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity;
  }
  // Remove blue tint from "floor" too
  else
  {
    vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity * (1.0 - LumaSettings.GameSettings.ColorGradingFilterReductionIntensity);
  }
#else // This branch removes the blue from the sky (and floor) as well
  vignetteColorIntensity = LumaSettings.GameSettings.ColorGradingIntensity * (1.0 - LumaSettings.GameSettings.ColorGradingFilterReductionIntensity);
#endif

  // Take away their color if we don't want any grading.
  // We restore the original luminance, even if these are gamma space multipliers and addends, however it still looks better than restoring their average.
  vignetteInnerRgbPlusMul = lerp(GetLuminance(vignetteInnerRgbPlusMul), vignetteInnerRgbPlusMul, vignetteColorIntensity);
  vignetteOuterRgbPlusAdd = lerp(GetLuminance(vignetteOuterRgbPlusAdd), vignetteOuterRgbPlusAdd, vignetteColorIntensity);
  
  // Find the brightness multiplication offset at the center of the screen and remove it from both mult factors if we are disabling color grading,
  // but default the game dimmed the image and "failed" to use the whole dynamic range.
  float3 vignetteCenterColorOffset = 1.0 - vignetteInnerRgbPlusMul;
  vignetteCenterColorOffset *= 1.0 - LumaSettings.GameSettings.ColorGradingIntensity;
  vignetteInnerRgbPlusMul += vignetteCenterColorOffset;
  vignetteOuterRgbPlusAdd += vignetteCenterColorOffset;
#endif

  // Blend between color mult at the center and color mult at the edges
  // Note: the multiplier "part" at the edges of the screen might be best applied after tonemapping with Luma, but whatever, this will work anyway!
  float3 vignetteMultiplier = lerp(vignetteInnerRgbPlusMul, vignetteOuterRgbPlusAdd, vignetteIntensity);
  composedColor *= vignetteMultiplier;
  //composedColor = vignetteMultiplier; // Test: view vignette color and intensity
#endif

  // Tint (by default this is 0 and matches the user brightness/contrast calibration)
  composedColor += Tint2dColour.xyz;
  
  // Luma: Tonemapping
  if (!forceVanilla)
  {
    composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1; // Vanilla clipped (hue shifted) look is better preserved with this
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      composedColor = DICETonemap(composedColor * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
      float shoulderStart = 0.667; // Set it higher than "MidGray", otherwise it compresses too much.
      if (tonemapPerChannel)
      {
        composedColor = Reinhard::ReinhardRange(composedColor, shoulderStart, -1.0, peakWhite / paperWhite, false);
      }
      else
      {
        composedColor = RestoreLuminance(composedColor, Reinhard::ReinhardRange(GetLuminance(composedColor), shoulderStart, -1.0, peakWhite / paperWhite, false).x, true);
        composedColor = CorrectOutOfRangeColor(composedColor, true, true, 0.5, peakWhite / paperWhite);
      }
    }

    composedColor = linear_to_gamma(composedColor, GCT_MIRROR);
  }

  o0.xyz = composedColor; 
#if 1 // Luma: keep alpha to 0 to separate UI and being able to tonemap it later (given the boost fire UI goes beyond 1) (hopefully no passes after tonemap needs the alpha)
  o0.w = 0;
#else
  o0.w = 1;
#endif

#if UI_DRAW_TYPE == 2 // TODO: theoretically we should undo this transformation for the FXAA shader (hash 0xF4CB0620) that runs later, however FXAA doesn't look good in this game and MSAA 8x look better
  // Note that this shader is also used in the game's display calibration menu!
  const float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;
  o0.rgb /= pow(UIPaperWhite, 1.0 / DefaultGamma);
  o0.rgb *= pow(gamePaperWhite, 1.0 / DefaultGamma);
#endif
}