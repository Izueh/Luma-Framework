#define LUT_3D 1
#define LUT_SIZE 16u

#include "Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/RCAS.hlsl"

// Doesn't look good, it creates steps
#if !defined(STRETCH_ORIGINAL_TONEMAPPER)
#define STRETCH_ORIGINAL_TONEMAPPER 0
#endif

#if !defined(SKIP_SECOND_TONEMAP_OUTPUT)
#define SKIP_SECOND_TONEMAP_OUTPUT 0
#endif

#define SAFE_FLT_MIN 9.99999975e-006

Texture3D<float4> t9 : register(t9); // 3D LUT 16x
Texture2D<float4> t8 : register(t8); // Film grain/dither map
Texture2D<float4> t7 : register(t7); // Lens flare
Texture2D<float4> t6 : register(t6); // Lens dirt
Texture2D<float4> t5 : register(t5); // Some noise map?
Texture2D<float4> t4 : register(t4); // (Auto) Exposure and tonemapper white level
Texture2D<float4> t3 : register(t3); // Blood overlay
Texture2D<float4> t2 : register(t2); // Bloom
Texture2D<float4> t1 : register(t1); // Scene
Texture2D<float4> t0 : register(t0); // Some overlay? It's seemengly 4x4

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

Texture2D<float2> dummyFloat2Texture : register(t10); // LUMA

cbuffer cb1 : register(b1)
{
  float4 cb1[7];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}

#define cmp

// Quick test to enable/disable Luma changes
float3 OptionalSaturate(float3 x)
{
#if ENABLE_LUMA
  return x;
#else // !ENABLE_LUMA
  return saturate(x);
#endif // ENABLE_LUMA
}
float3 OptionalMin1(float3 x)
{
#if ENABLE_LUMA
  return x;
#else // !ENABLE_LUMA
  return min(1.0, x);
#endif // ENABLE_LUMA
}
float3 OptionalMax0(float3 x)
{
#if ENABLE_LUMA
  return x;
#else // !ENABLE_LUMA
  return max(0.0, x);
#endif // ENABLE_LUMA
}

// One channel only, given they are all the same
float ApplyTonemap_Inverse(float x, float whiteLevel)
{
  float a = cb1[4].x;
  float b = cb1[4].y;
  float c = cb1[4].z;
  float d = cb1[5].x;
  float e = cb1[5].y;
  float f = cb1[5].z;

  float xSign = sign(x);
  x = abs(x);
  
  x = pow(x, 1.0 / cb0[2].x);

  float tonemappedWhiteLevel = Tonemap_Uncharted2_Eval(whiteLevel, a, b, c, d, e, f).x;
  
  return xSign * Tonemap_Uncharted2_Inverse_Eval(x, a, b, c, d, e, f) * tonemappedWhiteLevel;
}

float3 ApplyTonemap(float3 color, float whiteLevel, bool forceVanilla = false)
{
  float a = cb1[4].x;
  float b = cb1[4].y;
  float c = cb1[4].z;
  float d = cb1[5].x;
  float e = cb1[5].y;
  float f = cb1[5].z;

#if ENABLE_LUMA // scRGB support
  float3 colorSign = sign(color);
  color = abs(color);
#endif

  float tonemappedWhiteLevel = Tonemap_Uncharted2_Eval(whiteLevel, a, b, c, d, e, f).x;
  float3 tonemappedColor = Tonemap_Uncharted2_Eval(color, a, b, c, d, e, f);

  tonemappedColor = OptionalSaturate(tonemappedColor / tonemappedWhiteLevel);

#if !ENABLE_LUMA
  forceVanilla = true;
#endif

  if (forceVanilla)
    tonemappedColor = pow(tonemappedColor, cb0[2].x); // Gamma modulation (seemengly swings between 1 and 1.1 or so, depending on the lighting conditions)
  else // Change gamma correction to be by luminance to avoid further shifting colors // TODO: try this
    tonemappedColor = RestoreLuminance(tonemappedColor, pow(max(GetLuminance(tonemappedColor), 0.0), cb0[2].x), true);

  tonemappedColor = OptionalMin1(tonemappedColor);

#if ENABLE_LUMA // We can do this as this tm maps 0 to 0
  tonemappedColor *= colorSign;
#endif

  return tonemappedColor;
}

// Wrapper function to do the SDR tonemapper while also keeping HDR highlights
float3 ApplyTonemap_HDR_Proxy(float3 color, float whiteLevel, out float3 tonemappedSDRColor, bool forceSDR = false)
{
#if ENABLE_LUMA && EXPAND_COLOR_GAMUT // Run this in a wider color space, so shadow saturates more nicely, this massively changes red highlights, but they look more accurate, and nicer!
  float3 originalColor = color;
  if (!forceSDR)
    color = BT709_To_BT2020(color);
#endif

  // Match mid gray with the original TM output
  const float SDRTMMidGrayOut = MidGray; 
  float SDRTMMidGrayIn = ApplyTonemap_Inverse(SDRTMMidGrayOut, whiteLevel);
  float SDRTMMidGrayRatio = SDRTMMidGrayOut / SDRTMMidGrayIn;
  float3 tonemappedHDRColor = color * SDRTMMidGrayRatio;

#if ENABLE_LUMA && STRETCH_ORIGINAL_TONEMAPPER // Attept at stretching the SDR tonemapper // TODO: delete? Looks like crap (and no support for "forceSDR" currently)
  // Attept at stretching the SDR tonemapper
  float powCoeff = DVS9; // <=0.5
  float startingPoint = DVS10; // MidGray
  // Remap around the output's mid gray, so we keep the result "identical" below mid grey but expanded above it
  color *= SDRTMMidGrayRatio;
  color = (color > startingPoint) ? (pow(color - startingPoint + 1.0, powCoeff) + startingPoint - 1.0) : color;
  color /= SDRTMMidGrayRatio; // Restore back to the original/full range, to pass it to the game's SDR tonemapper again
#endif

  float3 tonemappedColor = ApplyTonemap(color, whiteLevel, forceSDR);
  tonemappedSDRColor = tonemappedColor;

#if ENABLE_LUMA && STRETCH_ORIGINAL_TONEMAPPER
  // This will possibly massively increase saturation, so make sure to tonemap by channel again in HDR later
  tonemappedHDRColor = (tonemappedColor > startingPoint) ? (pow(tonemappedColor - startingPoint + 1.0, 1.0 / powCoeff) + startingPoint - 1.0) : tonemappedColor;
#elif ENABLE_LUMA
  float shadowRestorationPow = 1.0;
#if GAMMA_CORRECTION_TYPE >= 1 // Hacky, but prevents overly crushed blacks when correcting for the sRGB->2.2 gamma mismatch (it seems like this game was sRGB) (we shouldn't branch based on gamma correction, but whatever)
  shadowRestorationPow = 0.25;
#endif
  tonemappedHDRColor = lerp(tonemappedColor, tonemappedHDRColor, pow(saturate(tonemappedColor / MidGray), shadowRestorationPow)); // Luma: restore untonemapped color from around 0.18
#endif

#if ENABLE_LUMA && EXPAND_COLOR_GAMUT
  if (!forceSDR)
  {
    tonemappedHDRColor = BT2020_To_BT709(tonemappedHDRColor);
#if 0 // Optionally run the raw SDR tonemapper output in the original color space, this kinda defeats the purpose of running in BT.2020 as we have hue restoration later, but... without this fires hue shifts too much
    tonemappedSDRColor = ApplyTonemap(originalColor, whiteLevel);
#else
    tonemappedSDRColor = BT2020_To_BT709(tonemappedSDRColor);
#endif
  }
#endif

  return forceSDR ? tonemappedSDRColor : tonemappedHDRColor;
}

float3 ApplyLUT(float3 HDRColor, float3 SDRColor, Texture3D<float4> _texture, SamplerState _sampler, bool forceSDR = false)
{
  float3 postLutColor;

#if ENABLE_LUT_EXTRAPOLATION
  bool lutExtrapolation = !forceSDR;
  if (lutExtrapolation)
  {
    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = HDRColor;
    extrapolationData.vanillaInputColor = SDRColor;
  
    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.lutSize = LUT_SIZE;
    extrapolationSettings.inputLinear = true;
    extrapolationSettings.lutInputLinear = false;
    extrapolationSettings.lutOutputLinear = false;
    extrapolationSettings.outputLinear = true;
    //extrapolationSettings.enableExtrapolation = DVS1 <= 0.5; // Test: Show clipping instead
    extrapolationSettings.extrapolationQuality = 2;
    // TODO: if we improved the desaturation of whites with extrapolation, we might be able to running the tonemapped by channel when "ENABLE_LUT_EXTRAPOLATION" is true (be careful to fires through!). We should also try this again given that we fix broken BT.2020 conversions, and see if we could go for "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE".
#if 1 // This one looks a lot better, it avoids highlights hue shifting and having a step
    extrapolationSettings.vanillaLUTRestorationAmount = 0.333; // This heavily desaturates highlights in HDR, due to the intense clipping. This goes up very fast so 0.1 is already a lot
#else
    extrapolationSettings.clampedLUTRestorationAmount = 0.333;
#endif
    postLutColor = SampleLUTWithExtrapolation(_texture, _sampler, extrapolationData, extrapolationSettings);
  }
  else
#endif
  {
    float3 lutInColor = linear_to_gamma(SDRColor, GCT_SATURATE);
    lutInColor = lutInColor * 0.9375 + 0.03125; // LUT_SIZE
    float3 lutOutColor = t9.Sample(s2_s, lutInColor).rgb;
    lutOutColor = gamma_to_linear(lutOutColor, GCT_MIRROR);

    // If "forceSDR" is true, the code below wouldn't do anything anyway as the sdr and hdr colors will match, but let's do an early return for safety.
    if (forceSDR) return lutOutColor;

    float hueRestoration = 0.333; // It doesn't really seem to be needed in this game, we could even set it to 0
    bool BT2020 = false;
#if EXPAND_COLOR_GAMUT
    BT2020 = true;
#endif
    postLutColor = RestorePostProcess(HDRColor, SDRColor, lutOutColor, hueRestoration, BT2020);
  }
  
  return postLutColor;
}

// Note: this is also used by the rear view mirror rendering, as most of the game post processing is.
// Note: this has lens distortion in the vertex shader, used when doing car speed slow down (super power).
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4,r5,r6;

  o0.w = 1;
  o1.w = 1;

  float3 sceneColor = t1.Sample(s1_s, v1.yz).rgb;
  
#if 0 // Disable all - linear untonemapped raw HDR
  o0.rgb = sceneColor;
  o1.rgb = sceneColor;
  return;
#endif

  float sourceWidth, sourceHeight;
  t1.GetDimensions(sourceWidth, sourceHeight);
  float2 uv = v0.xy / float2(sourceWidth, sourceHeight); // v1 has lens distortion

  // In these cases, AA run before TAA (e.g. game cutscenes), so we do some more stuff here
  bool forceFinalPostProcess = (LumaData.CustomData1 & 0x1) != 0; // First bit (bit 0)
  bool forceEncode = (LumaData.CustomData1 & 0x2) != 0; // Second bit (bit 1)
  bool forceSDR = ShouldForceSDR(uv) || LumaSettings.DisplayMode != 1;
#if !ENABLE_LUMA
  forceSDR = true;
#endif

  // We couldn't apply sharpening any later in this edge case, as it needs to sample it from a texture (unless we wanted to compute tonemapping 4-5 times per pixel...), this should be fine,
  // except for where there is lens distortion applied in the vertex shader (somehow?), either way it won't look terrible.
  if (forceFinalPostProcess) {
#if ENABLE_SHARPENING
    float2 distortedPos = v1.yz * float2(sourceWidth, sourceHeight);

    float sharpenAmount = LumaSettings.GameSettings.Sharpening;
	  sceneColor.rgb = RCAS(distortedPos, 0, 0x7FFFFFFF, sharpenAmount, t1, dummyFloat2Texture, 1.0, true, float4(sceneColor, 1.0)).rgb;
#endif // !ENABLE_SHARPENING
  }
  
  bool applyChromaticAberration = cb0[3].x > 0.0;
  bool applyOverlay = cb0[3].y > 0.0;
  bool applyBloom = cb0[3].z > 0.0;
  bool applyTonemap = cb0[3].w > 0.0;
  bool applyLevels = cb0[4].x > 0.0;
  bool applyLUT = cb0[4].y > 0.0;
  bool applyBlackAndWhite = cb0[4].z > 0.0;
  bool applyFilmGrain = cb0[4].w > 0.0;
  bool applySomething = cb0[5].x > 0;
  bool applyLUTByMaxChannel = cb0[5].y > 0; // Disabled by default, for unused original HDR
  bool applyDithering = cb0[6].z > 0.0;
  
#if ENABLE_CHROMATIC_ABERRATION // Chromatic Aberration (doesn't have a user toggle)
  if (applyChromaticAberration) {
    r2.xy = max(float2(0.01, 0.0), cb1[6].zw);
    r2.zw = v1.yz * 2.0 - 1.0; // From 0|1 to -1|1
    r2.zw = sqr(r2.zw);
    r1.x = r2.z + r2.w;
    r1.x = sqrt(r1.x);
    r2.y = 0.005 * r2.y;
    r1.x = max(SAFE_FLT_MIN, r1.x);
    r1.x = pow(r1.x, r2.x);
    r2.z = r2.y * r1.x;
    r2.x = -r2.z;
    r2.yw = 0.0;
    r2.xyzw = v1.yzyz + r2.xyzw;
    // Sample the scene again with shifted UVs, then blend the red and blue channels at 50%
    float3 shiftedSceneColor = sceneColor;
    shiftedSceneColor.r = t1.Sample(s1_s, r2.xy).r;
    shiftedSceneColor.b = t1.Sample(s1_s, r2.zw).b;
    sceneColor.rb = (shiftedSceneColor.rb + sceneColor.rb) * 0.5;
  }
#endif
  
  // Lens flare+dirt, and blood overlay
  // TODO: allow disabling these, and also make sure they don't stretch in UW
  if (applyOverlay) {
    r2.xyz = t7.Sample(s2_s, v1.yz).xyz;
    r1.x = t3.Sample(s2_s, v1.yz).x;
    r1.y = t6.Sample(s2_s, v1.yz).x;
    r1.x = 0.1 * r1.x;
    r1.x = r1.x * r1.x;
    r2.xyz = r1.x * r2.xyz;
    r1.x = r1.y * 100.0 + 5.0;
    r2.xyz = r2.xyz * r1.x;
    sceneColor.rgb += r2.xyz * cb1[0].z;
  }
  
  // Bloom and something else
  if (applyBloom) {
    float3 bloomColor = t2.Sample(s2_s, v1.yz).xyz;
    float3 someOverlay = t0.Sample(s2_s, v1.yz).xyz;
    bloomColor += someOverlay * cb0[2].y;
#if ENABLE_LUMA && 1 // Luma: fix bloom raising blacks too much (at the cost of having worse bloom gradients) // TODO: fix. Maybe try to spread bloom less in its bloom gen
    float bloomShadowPoint = 0.2;
    float3 scaledBloom = bloomColor / bloomShadowPoint;
    float bloomLuminance = GetLuminance(scaledBloom);
    sceneColor.rgb += (bloomLuminance >= 1.0) ? bloomColor : (RestoreLuminance(scaledBloom, pow(max(bloomLuminance, 0.0), 1.125), true) * bloomShadowPoint);
#elif ENABLE_LUMA && 0 // Random bad version (this prevents bloom in the darkness)
    sceneColor.rgb += bloomColor * saturate(sceneColor.rgb * 10.0);
#else
    sceneColor.rgb += bloomColor;
#endif
  }

  // Levels (multiply and add)
  if (applyLevels) 
  {
    // TODO: check if yellowing ever comes from LUTs or here too
    float3 preLevelsColor = sceneColor.rgb;
    sceneColor.xyz *= cb1[2].xyz;
    float3 preAdditiveColor = sceneColor.xyz;
    sceneColor.xyz += cb1[1].xyz;
#if ENABLE_LUMA // Restore the distorted hue, without affecting chrominance (it'd desaturate) or luminance
    sceneColor.xyz = RestoreHueAndChrominance(preAdditiveColor, sceneColor.xyz, forceSDR ? 0.0 : 0.75, 0.0); // TODO: also make sure that doing it at 100% works

    float lutIntensity = LumaData.CustomData3;
    sceneColor.rgb = lerp(preLevelsColor, sceneColor.rgb, lutIntensity);
#endif
    sceneColor.xyz = OptionalMax0(sceneColor.xyz);
  }
  
  // Tonemapper (and exposure)
  float3 SDRColor = sceneColor.xyz;
  float3 tonemappedColor;
  if (applyTonemap) {
#if !ENABLE_LUMA
    sceneColor.xyz = min(FLT16_MAX, sceneColor.xyz);
#endif
    float2 exposureAndWhiteLevel = t4.Sample(s1_s, 0.5).yw; // Red channel seemengly unused
    float whiteLevel = exposureAndWhiteLevel.y * exposureAndWhiteLevel.x;
    sceneColor.xyz *= exposureAndWhiteLevel.x;
    tonemappedColor = ApplyTonemap_HDR_Proxy(sceneColor.xyz, whiteLevel, SDRColor, forceSDR);

#if 0 // Test: visualize tonemapper // TODO: move to a global function(s)
    float2 UV = float2(v1.y, 1.0 - v1.z);
    float testWidth, testHeight;
    t1.GetDimensions(testWidth, testHeight);
    float outputAspectRatio = testWidth / testHeight;
    float size = 0.3; // Takes this % of screen in vertical space
    float2 position = float2(0.85, 0.9); // Start from this position of the screen (on both axes) (positive is usually top right)
    float2 scaledSize = float2(size / outputAspectRatio, size);
    position -= lerp(0.0, scaledSize, position); // Avoid position going overboard (0.5 might not be fully centered anymore, doesn't matter)
    float2 minUV = position;
    float2 maxUV = position + scaledSize;
    if (all(UV >= minUV && UV <= maxUV))
    {
      float2 remappedUV = float2(InverseLerp(minUV.x, maxUV.x, UV.x), InverseLerp(minUV.y, maxUV.y, UV.y));
      //remappedUV.y = 1.0 - remappedUV.y; // Flip Y to make it grow towards the top of the screen if you need

      float inBrightnessScale = DVS3 == 0.0 ? 1.0 : (DVS3 * 100.0); // Note: link this to "paperWhite" instead?
      float outBrightnessScale = MidGray * 3; // A random scale

#if 0 // Generate hues, saturations and brightnesses
      // The horizontal axis will cover all hues ahd saturations, and vertical axis will go up in brightness
      bool scaleAfter = true; // HSV makes little sense so this is trying to repair it
      sceneColor.xyz = HSV_To_RGB(float3(frac(remappedUV.x * 1.0 + 1.0 / 6.0), 1.0, scaleAfter ? 1.0 : remappedUV.y)) * inBrightnessScale * (scaleAfter ? remappedUV.y : 1.0);
#else // Generate raw hues only, which should also cover saturations (horizontally)
      sceneColor.xyz = HueToRGB(remappedUV.x); // Output is in 0-1 range
      //sceneColor.xyz = pow(sceneColor.xyz, 1.0 / DVS4); // Saturation (doesn't work, the hues generated by this are still not all of them)
      sceneColor.xyz *= lerp(0, inBrightnessScale, remappedUV.y);
#endif

      // Lerp between Untonemapped, HDR tonemapped and SDR tonemapped
      tonemappedColor = ApplyTonemap_HDR_Proxy(sceneColor.xyz, whiteLevel, SDRColor);
      tonemappedColor = lerp(sceneColor.xyz, tonemappedColor, DVS5);
      tonemappedColor = lerp(tonemappedColor, SDRColor, DVS6 * DVS5);

      // Normalize it by luminance, on mid grey,
      // so that there's no hue shifts from the screen itself.
      // This means any change to brightness from tonemapping will be lost, this is just to analyze hue shifts.
      float luminance = max(GetLuminance(tonemappedColor), 0.0);
      tonemappedColor = safeDivision(tonemappedColor, luminance / outBrightnessScale, 0);

      // Divide by pw so we don't it won't be affected by it (keep it at fixed 80 nits)
      const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
      tonemappedColor /= paperWhite;

      o0.rgb = tonemappedColor;
      o1.rgb = o0.rgb;
      return;
    }
#endif

#if ENABLE_LUMA && !STRETCH_ORIGINAL_TONEMAPPER // Luma: restore SDR colors (these are not gonna do anything if "forceSDR" is on)
    tonemappedColor = RestoreHueAndChrominance(tonemappedColor, SDRColor, 0.8, 0.4); // TODO: do this later in LUT extrapolation? It's currently not the same thing
#endif
  } else {
    tonemappedColor = sceneColor.xyz;
  }
  
  // LUT
  if (applyLUT) {
    float lutIntensity = 1.0;
#if ENABLE_LUMA
    lutIntensity = LumaData.CustomData3;
#endif
    lutIntensity *= saturate(cb1[6].y);
#if ENABLE_LUMA // This branch isn't needed with Luma, even if it was ever active
    applyLUTByMaxChannel = false; // TODO: actually try this with Luma
#endif
    // This is supposedly the game's original HDR implementation, in case it skipped the tonemapper or something, however it seems like it was never used.
    // The same identical code is also in "Dying Light: The Beast", so it was either written by a shared developer, or can be found somewhere online (I couldn't find a rerence).
    // It seems to look good, however it doesn't support negative input values.
    // Update: it's from https://www.glowybits.com/blog/2016/12/21/ifl_iss_hdr_1/ (Infamous PS4 HDR)
    if (applyLUTByMaxChannel) {
      float3 color = tonemappedColor;
      float max_channel = max3(color.r, color.g, color.b);
      max_channel = max(1e-6, max_channel);

      // Soft-knee highlight compression
      const float KNEE = 0.525;
      const float A = 1.0 / KNEE;     // 1.90476203
      const float B = 122.0 / 21.0;  // 5.80952358
      const float C = 9.0 / 21.0;    // 0.429761916
      float scale;
      if (abs(1.0 - max_channel) < KNEE) {
        scale = ((-max_channel * A + B) * max_channel - C) * 0.25;
      } else {
        scale = min(1.0, max_channel);
      }
      scale /= max_channel;

      color *= scale;

      color = max(SAFE_FLT_MIN, color);
      color = pow(color, 1.0 / 2.2);
      color = color * 0.9375 + 0.03125; // LUT_SIZE
      color = t9.Sample(s2_s, color).xyz;
      color = max(SAFE_FLT_MIN, color);
      color = pow(color, 2.2);

      tonemappedColor = lerp(tonemappedColor, color / scale, saturate(lutIntensity));
    } else {
      float3 luttedColor;
#if ENABLE_LUMA
      luttedColor = lutIntensity > 0.f ? ApplyLUT(tonemappedColor, SDRColor, t9, s2_s, forceSDR) : tonemappedColor;
#else
      luttedColor = max(SAFE_FLT_MIN, tonemappedColor);
      luttedColor = pow(luttedColor, 1.0 / 2.2);
      luttedColor = luttedColor * 0.9375 + 0.03125; // LUT_SIZE
      luttedColor = t9.Sample(s2_s, luttedColor).xyz;
      luttedColor = max(SAFE_FLT_MIN, luttedColor);
      luttedColor = pow(luttedColor, 2.2);
#endif

#if ENABLE_LUMA // Attempt to undo the yellow filter the game LUT applied
      float3 lutMidGreyGamma = t9.Sample(s2_s, 0.5).rgb;
      float3 lutMidGreyLinear = pow(abs(lutMidGreyGamma), 2.2) * sign(lutMidGreyGamma); // Turn linear
      float lutMidGreyBrightnessLinear = max(GetLuminance(lutMidGreyLinear), 0.0); // Normalize it by luminance
      float yellowCorrectionIntensity = LumaData.CustomData4; // Note that this will correct other color filters as well!
#if 1
      luttedColor /= (lutMidGreyLinear != 0.0) ? lerp(1.0, safeDivision(lutMidGreyLinear, lutMidGreyBrightnessLinear, 1), yellowCorrectionIntensity) : 1.0;
#else // Do it in gamma space to better emulate the LUT color shift // TODO: test both and also simply avoid linearizing the lut output color above to prevent a double conversion, actually it seems like the result is identical...
      luttedColor = pow(abs(luttedColor), 1.0 / 2.2) * sign(luttedColor);
      float lutMidGreyBrightnessGamma = pow(lutMidGreyBrightnessLinear, 1.0 / 2.2);
      luttedColor /= (lutMidGreyGamma != 0.0) ? lerp(1.0, safeDivision(lutMidGreyGamma, lutMidGreyBrightnessGamma, 1), yellowCorrectionIntensity) : 1.0;
      luttedColor = pow(abs(luttedColor), 2.2) * sign(luttedColor);
#endif
#endif // ENABLE_LUMA

      tonemappedColor = OptionalSaturate(lerp(tonemappedColor, luttedColor, lutIntensity)); // LUT intensity
    }
  }
  
  r0.w = saturate(cb1[6].x);
#if !ENABLE_LUMA
  r3.xyz = min(FLT16_MAX, tonemappedColor);
#else
  r3.xyz = tonemappedColor;
#endif
  r3.xyz = lerp(tonemappedColor, GetLuminance(r3.xyz), r0.w);
  tonemappedColor = applyBlackAndWhite ? r3.xyz : tonemappedColor;
  
#if ENABLE_FILM_GRAIN
  if (applyFilmGrain) {
    r3.xy = cb0[0].x * v1.yz; // Shift film grain every frame // TODO: make sure this looks right at 4k if it ever triggers, the cbuffer variable would ideally already be scaled by vertical resolution
#if ENABLE_LUMA // Fixed film grain stretching in Ultrawide (in fact, at any non 1 aspect ratios)
    float sourceWidth, sourceHeight;
    t1.GetDimensions(sourceWidth, sourceHeight);
    float outputAspectRatio = sourceWidth / sourceHeight;
    r3.x *= outputAspectRatio;
#endif
    r4.x = dot(cb0[1].xy, r3.xy);
    r4.y = dot(cb0[1].zw, r3.xy);
    float3 filmGrain = t8.Sample(s0_s, r4.xy).xyz;
#if ENABLE_LUMA // Fix film grain being additive and raising blacks
    filmGrain = filmGrain * 2.0 - 1.0; // From 0|1 to -1|1
#endif
    tonemappedColor += filmGrain * cb0[0].y;
#if ENABLE_LUMA && (DEVELOPMENT || TEST) // Turn purple to see it
    tonemappedColor = float3(1, 0, 1);
#endif
  }
#endif

#if ENABLE_LUMA
  if (forceFinalPostProcess && !forceSDR) {
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

#if !STRETCH_ORIGINAL_TONEMAPPER && !ENABLE_LUT_EXTRAPOLATION
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE); // We already tonemapped by channel and restored hue/chrominance so let's not shift it anymore by tonemapping by channel
#else
    DICESettings settings = DefaultDICESettings();
#endif // !STRETCH_ORIGINAL_TONEMAPPER && !ENABLE_LUT_EXTRAPOLATION
#if 0
    settings.ShoulderStart = paperWhite / peakWhite; // Only tonemap beyond paper white, so we leave the SDR range untouched (roughly), so we leave the SDR range untouched (roughly), even if we only blend in the SDR tonemapper up to mid grey, if we start earlier HDR would lose range
#endif
    tonemappedColor.rgb = DICETonemap(tonemappedColor.rgb * paperWhite, peakWhite, settings) / paperWhite;
  }
#endif // ENABLE_LUMA
  
  // Probably used for fades to black
  float3 colorFilter = saturate(lerp(1.0, cb1[3].xyz, v1.x));
  tonemappedColor *= applySomething ? colorFilter : 1.0;

#if ENABLE_DITHERING // Dithering is not really needed in HDR with higher quality textures, also it was 8 bit
  if (applyDithering) {
    int4 r0i = int4((int2)cb0[6].xy + v0.xy, 0, 0); // Temporally shift dithering
    r0i.xy = r0i.xy & int2(63,63);
    float3 dither = t5.Load(r0i.xyz).xyz;
    dither = dither * 2.0 - 1.0; // From 0|1 to -1|1
#if ENABLE_LUMA
    r2.xyz = sqrt(abs(tonemappedColor)) * sign(tonemappedColor);
#else
    r2.xyz = sqrt(max(0, tonemappedColor));
#endif
    float3 ditherScale = min(cb0[6].z, r2.xyz + cb0[6].w);
    r2.xyz += dither * ditherScale; // Apply dither in gamma space
#if ENABLE_LUMA
    tonemappedColor = sqr(r2.xyz) * sign(r2.xyz);
#else
    tonemappedColor = sqr(r2.xyz);
#endif
  }
#endif
  
  o0.xyz = tonemappedColor;

  if (forceEncode) {
#if UI_DRAW_TYPE == 2 
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

    o0.rgb = linear_to_sRGB_gamma(o0.rgb, GCT_MIRROR);
  }

#if !SKIP_SECOND_TONEMAP_OUTPUT
  // Second image!??? This is optionally meant as source for next frame's TAA? It doesn't seem to be used.
  float3 tonemappedColor2 = sceneColor;
  {
    // Tonemapper (and exposure)
    if (applyTonemap) {
#if !ENABLE_LUMA
      tonemappedColor2 = min(FLT16_MAX, tonemappedColor2);
#endif
      float2 exposureAndWhiteLevel = t4.Sample(s1_s, 0.5).yw; // Red channel seemengly unused
      float whiteLevel = exposureAndWhiteLevel.y * exposureAndWhiteLevel.x;
      tonemappedColor2 *= exposureAndWhiteLevel.x;
      tonemappedColor2 = ApplyTonemap(tonemappedColor2, whiteLevel); // TODO: if ever needed, we should make this and the rest here HDR too?
    }

    // LUT
    if (applyLUT) {
#if ENABLE_LUMA
      r4.xyz = tonemappedColor2;
#else
      r4.xyz = max(SAFE_FLT_MIN, tonemappedColor2);
#endif
      r4.xyz = pow(r4.xyz, 1.0 / 2.2);
      r4.xyz = r4.xyz * 0.9375 + 0.03125; // LUT_SIZE
      r4.xyz = t9.Sample(s2_s, r4.xyz).xyz;
      r4.xyz = max(SAFE_FLT_MIN, r4.xyz);
      r4.xyz = pow(r4.xyz, 2.2);
      tonemappedColor2 = OptionalSaturate(lerp(tonemappedColor2, r4.xyz, saturate(cb1[6].y))); // LUT intensity
    }

#if !ENABLE_LUMA
    r4.xyz = min(FLT16_MAX, tonemappedColor2);
#else
    r4.xyz = tonemappedColor2;
#endif
    r4.xyz = lerp(tonemappedColor2, GetLuminance(r4.xyz), r0.w);
    tonemappedColor2 = applyBlackAndWhite ? r4.xyz : tonemappedColor2;

#if ENABLE_FILM_GRAIN
    if (applyFilmGrain) {
      r2.yz = cb0[0].x * v1.yz; // Shift film grain every frame
      r4.x = dot(cb0[1].xy, r2.yz);
      r4.y = dot(cb0[1].zw, r2.yz);
      float3 filmGrain = t8.Sample(s0_s, r4.xy).xyz;
#if ENABLE_LUMA // Fix film grain being additive and raising blacks
      filmGrain = filmGrain * 2.0 - 1.0; // From 0|1 to -1|1
#endif
      tonemappedColor2 += filmGrain * cb0[0].y;
    }
#endif
    tonemappedColor2 *= applySomething ? colorFilter : 1.0;
    o1.xyz = tonemappedColor2;
  }
#else // Optimization, given it doesn't seem to be ever used?
  o1.xyz = sceneColor;
#endif
}