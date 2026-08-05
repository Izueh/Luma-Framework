#include "../../Includes/Color.hlsl"
// Spectre begin
namespace Color
{

// Luma-Framework - Filippo Tarpini
// This does gamut mapping, however it's not focused on gamut as primaries, but on peak white (TargetRange).
// The color is expected to be in the specified color space and in linear.
// This works best when called after grading or tonemapping (by channel) in a wider color space than the current one.
float3 CorrectOutOfRangeColor(float3 Color, float3 LuminanceVec, bool FixNegatives = true, bool RecoverInvalidNegatives = true, bool FixPositives = true, float TargetRange = 1.0)
{
    float minChannel = min(Color.r, min(Color.g, Color.b));
    if (FixNegatives && minChannel < 0.0) // Optional "optimization" branch ("desaturateAlpha" will then need saturating)
    {
        float colorLuminance = dot(Color, LuminanceVec);
        // Desaturate (move towards luminance/grayscale) until we are not out of gamut anymore (until no channel is below 0)
        if (colorLuminance >= 0.0)
        {
            float desaturateAlpha = (minChannel - colorLuminance) != 0.0 ? (minChannel / (minChannel - colorLuminance)) : 0.0; // Both division elements are meant to be negative so the ratio resolves to a positive value
            Color = lerp(Color, colorLuminance, desaturateAlpha);                                                              // Note: can produce slightly below 0 results, clamp if needed
        }
        else if (RecoverInvalidNegatives)
        {
            Color = CorrectNegativeLuminance(Color, LuminanceVec);
        }
        else
        {
#if 0 // Enable this if the game was made to clip gamut (e.g. negative color offsets or rgb tint matrices) (though we have "RecoverInvalidNegatives" for that!)
			Color = max(Color, 0.0);
#else
            // Snap to 0 if the overall luminance was zero (or possibly less), there's nothing to salvage, no valid information on rgb ratio
            // (though we need to be careful with this as sometimes colors got subtracted and then expected the final result to clip away negative values, due to storing the result in UNORM render targets)
            Color = 0.0;
#endif
        }
    }

    float maxChannel = max(Color.r, max(Color.g, Color.b)); // This is guaranteed to be >= "colorLuminance"
    if (FixPositives && maxChannel > TargetRange)           // Optional "optimization" branch
    {
        float colorLuminance = dot(Color, LuminanceVec); // Expected to be > 0 if we got here, otherwise run "FixNegatives".

        // Find out the required desaturation amounts to contain the color within the max range value.
        float colorLuminanceInExcess = colorLuminance - TargetRange;
        float maxColorInExcess = maxChannel - TargetRange;                                             // This is guaranteed to be >= "colorLuminanceInExcess"
        float excessDiff = maxColorInExcess - colorLuminanceInExcess;                                  // Equals "maxChannel - colorLuminance"
        float desaturateAlpha = saturate((excessDiff != 0.f) ? (maxColorInExcess / excessDiff) : 0.0); // Fall back to zero in case of division by zero
        // Desaturate to contain rgb within the peak, on each channel
        Color = lerp(Color, colorLuminance, desaturateAlpha);
    }

    return Color;
}
}

float Square(float x) { return x * x; }

float3 NakaRushton(float3 x, float3 peak = 1.0, float3 anchorIn = 0.18, float3 anchorOut = 0.18, float3 exponent = 1.0)
{
	float3 n = (exponent * peak) / (peak - anchorOut);
	float3 adjustedAnchorOut = pow(x, n) * anchorOut;
	return (peak * adjustedAnchorOut) / ((pow(anchorIn, n) * (peak - anchorOut)) + adjustedAnchorOut);
}

float3 RemapClamped(float3 input, float3 oldMin, float3 oldMax, float3 newMin, float3 newMax)
{
	float3 range = newMax - newMin;
	float3 t = (input - oldMin) / (oldMax - oldMin);
	t = min(max(t, 0.0), 1.0); // Avoids NaN if oldMin matches oldMax, and also clamps to 0-1 range
	return (t * range) + newMin;
}
float3 RemapClampedExponent(float3 input, float3 oldMin, float3 oldMax, float3 newMin, float3 newMax, float exponent = 1.0)
{
	float3 range = newMax - newMin;
	float3 t = (input - oldMin) / (oldMax - oldMin); // Normalize to 0-1 space to do exponent
	t = min(max(t, 0.0), 1.0); // Avoids NaN if oldMin matches oldMax, and also clamps to 0-1 range
	t = pow(t, exponent);
	return (t * range) + newMin;
}

float3x3 Invert3x3Matrix(float3x3 m)
{
	float a = m[0][0], b = m[0][1], c = m[0][2];
	float d = m[1][0], e = m[1][1], f = m[1][2];
	float g = m[2][0], h = m[2][1], i = m[2][2];

	float A = (e * i - f * h);
	float B = -(d * i - f * g);
	float C = (d * h - e * g);
	float D = -(b * i - c * h);
	float E = (a * i - c * g);
	float F = -(a * h - b * g);
	float G = (b * f - c * e);
	float H = -(a * f - c * d);
	float I = (a * e - b * d);

	float determinant = a * A + b * B + c * C;
	float determinantInv = determinant != 0.0 ? (1.0 / determinant) : 0.0;

	return float3x3(
		A, D, G,
		B, E, H,
		C, F, I) * determinantInv;
}

static const float2 WHITE_POINT_D65 = float2(0.31272f, 0.32903f);
// Note: theoretically it's possible to change these to any color space, be it sRGB or BT.2020, or even a matrix to emulate a specific film stock.
static const float3 CIE1702_MB_WEIGHTS = float3(0.68990272f, 0.34832189f, 0.0371597f); // Sums to 1.07~
static const float3 CIE1702_MB_NORMALIZED_WEIGHTS = CIE1702_MB_WEIGHTS / dot(1.0, CIE1702_MB_WEIGHTS);
static const float3x3 XYZ_TO_STOCKMAN_SHARP_LMS_MAT = float3x3(
	0.2670502842655792f, 0.8471990148492798f, -0.03470416612462053f,
	-0.38706882411220156f, 1.165429935890458f, 0.10302286696614202f,
	0.026727793989083093f, -0.02729131667566509f, 0.5333267257603284f);
static const float3x3 STOCKMAN_SHARP_LMS_TO_XYZ_MAT = Invert3x3Matrix(XYZ_TO_STOCKMAN_SHARP_LMS_MAT);

float3 LMSToMacLeodBoynton(float3 lms)
{
	float3 lmsWeighted = CIE1702_MB_WEIGHTS * lms;
	float y = lmsWeighted.x + lmsWeighted.y; // L+M luminance (achromatic axis)
	if (y <= 1e-12)
	{
		// Return white D65 chromaticity with no luminance (compiler precomputed)
		float3 xyz = xyYToXYZ(float3(WHITE_POINT_D65, 1.0));
		lms = mul(XYZ_TO_STOCKMAN_SHARP_LMS_MAT, xyz);
		lmsWeighted = CIE1702_MB_WEIGHTS * lms;
		y = lmsWeighted.x + lmsWeighted.y;
		return float3(lmsWeighted.x / y, lmsWeighted.z / y, 0.0);
	}
	float l = lmsWeighted.x / y; // L/(L+M) chromaticity ("red–green" axis)
	float s = lmsWeighted.z / y; // S/(L+M) chromaticity ("yellow–blue" axis)
	return float3(l, s, y);
}
float3 MacLeodBoyntonToLMS(float3 lsy)
{
	float l = lsy.x;			// L/(L+M) chromaticity ("red–green" axis)
	float s = lsy.y;			// S/(L+M) chromaticity ("yellow–blue" axis)
	// Clamp to avoid everything going negative (usually not necessary!)
	float y = max(lsy.z, 0.0);	// L+M luminance (achromatic axis)

	float L = (l * y) / CIE1702_MB_WEIGHTS.x;
	float M = ((1.0 - l) * y) / CIE1702_MB_WEIGHTS.y;
	float S = (s * y) / CIE1702_MB_WEIGHTS.z;
	return float3(L, M, S);
}

// Does not necessarily return 1 for 1 1 1 if not normalized, so only use it as a relative ratio.
float LMSToLuminance(float3 lms, bool normalized = true)
{
	return dot(lms, normalized ? CIE1702_MB_NORMALIZED_WEIGHTS : CIE1702_MB_WEIGHTS);
}

float2 WhiteD65MacLeodBoyntonChromaticity()
{
	float3 xyz = xyYToXYZ(float3(WHITE_POINT_D65, 1.0));
	float3 lms = mul(XYZ_TO_STOCKMAN_SHARP_LMS_MAT, xyz);
	return LMSToMacLeodBoynton(lms).xy;
}

// Reproject the xy direction ("hue") around D65 white,
// the xy length ("chroma"/"purity"/"saturation"),
// and the z/Y ("luminance").
// Keep the amounts between 0 and 1.
// "staticParameters" can make optimizations depending on whether we know the input params are fixed or change.
// "chromaScale" allows scaling of the output chroma, independently from anything else (it's just an optimization to fold chroma changes in this function, given it already converts to a chroma scaling friendly representation).
float3 RestoreHueChromaLuminanceLMS(float3 reference, float3 reprojectionTarget, bool staticParameters = true, float hueAmount = 1.0, float chromaAmount = 0.0, float luminanceAmount = 0.0, bool allowChromaIncrease = true, bool allowChromaDecrease = true, float chromaScale = 1.0)
{
	float3 mbSource = LMSToMacLeodBoynton(reference);
	float3 mbTarget = LMSToMacLeodBoynton(reprojectionTarget);
	float2 mbWhite = WhiteD65MacLeodBoyntonChromaticity();

	// "white" is the center of the hue "circle"
	float2 sourceOffset = mbSource.xy - mbWhite;
	float2 targetOffset = mbTarget.xy - mbWhite;
	float sourceSquaredLength = dot(sourceOffset, sourceOffset);
	float targetSquaredLength = dot(targetOffset, targetOffset);
	float blendedLuminance = lerp(mbTarget.z, mbSource.z, luminanceAmount); // Luminance lerp
	// Just skip reprojecting if either lengths are next to zero, the difference wouldn't be perceptible and we risk going ~INF.
	// We don't check for "hueAmount" or "chromaAmount" being 0 as we assume at least one is > 0.
	if (sourceSquaredLength > 1e-9 && targetSquaredLength > 1e-9)
	{
		// Use *rsqrt instead of /sqrt as a potential optimization (compiler would likely do the same anyway) (also we might re-do both sqrts below, so maybe we should just cache it once)
		float2 sourceDir = sourceOffset * rsqrt(sourceSquaredLength);
		float2 targetDir = targetOffset * rsqrt(targetSquaredLength);
		
		float2 blendedDenormDir = lerp(targetDir, sourceDir, hueAmount); // Hue lerp
		float blendedDenormSquaredLength = dot(blendedDenormDir, blendedDenormDir);
		// Re-normalize blendedDenormDir given that lerping two directions doesn't produce a normalized output
		float2 blendedDir;
		if (blendedDenormSquaredLength > 1e-9 && (!staticParameters || hueAmount > 0.0)) // Force "targetDir" if we aren't restoring any hue and we have static params, so we skip the operations above
			blendedDir = blendedDenormDir * rsqrt(blendedDenormSquaredLength); // Potential optimization: lerp the rsqrt to 1 based on 1-chromaAmount?
		else
			blendedDir = targetDir;

		float targetLength = sqrt(targetSquaredLength);
		float sourceLength = sqrt(sourceSquaredLength);
		float blendedLength = lerp(targetLength, sourceLength, chromaAmount); // Chroma lerp
		if (!allowChromaIncrease)
			blendedLength = min(blendedLength, targetLength);
		if (!allowChromaDecrease)
			blendedLength = max(blendedLength, targetLength);
		float2 blendedOffset = blendedDir * blendedLength * chromaScale;
		float3 mbReprojected = float3(blendedOffset + mbWhite, blendedLuminance);
		return MacLeodBoyntonToLMS(mbReprojected);
	}
	// Restore luminance only on colors with no hue
	else if (luminanceAmount > 0.0 || chromaScale != 1.0)
	{
		float3 mbReprojected = float3((targetOffset * chromaScale) + mbWhite, blendedLuminance);
		return MacLeodBoyntonToLMS(mbReprojected);
	}
	// Nothing to restore
	return reprojectionTarget;
}

float3 BT709ToLMS(float3 rgb)
{
    float3 xyz = mul(BT709_2_XYZ, rgb);
	return mul(XYZ_TO_STOCKMAN_SHARP_LMS_MAT, xyz);
}
float3 LMSToBT709(float3 lms)
{
    float3 xyz = mul(STOCKMAN_SHARP_LMS_TO_XYZ_MAT, lms);
    return mul(XYZ_2_BT709, xyz);
}
float3 BT2020ToLMS(float3 rgb)
{
	float3 xyz = mul(BT2020_2_XYZ, rgb);
	return mul(XYZ_TO_STOCKMAN_SHARP_LMS_MAT, xyz);
}
float3 LMSToBT2020(float3 lms)
{
	float3 xyz = mul(STOCKMAN_SHARP_LMS_TO_XYZ_MAT, lms);
	return mul(XYZ_2_BT2020, xyz);
}

struct SpectreSettings
{
    uint colorSpace;
    bool gamutMapIn;
    float gamutMapOutAmount;
    float gamutMapOutDechromaVsDarkening;
    float whiteLevel;
    float midGreyIn;
    float midGreyOut;
    float shadowContrast;
    float highlightContrast;
    float shadowSaturation;
    float highlightSaturation;
    float hueShiftsInMinNits;
    float hueShiftsInMaxNits;
    float hueShiftsOutMinNits;
    float hueShiftsOutMaxNits;
    float highlightDechroma;
    float neutralShadowAmount;
    float blueHueShiftsCorrection;
    float3x3 insetMatrix;
    bool staticSettings;
};

SpectreSettings GetTonemapSpectreLMSDefaultSettings()
{
    SpectreSettings settings;

    // Determines the input and output color space,
    // and the output gamut mapping target.
    // 0 sRGB/BT.709
    // 1 BT.2020
    settings.colorSpace = 0;

    // A simple RGB/xy based gamut map that desaturates until both < 0 and > peak white values are back in range,
    // it looks fine as long as the input colors were mostly in range, and the LMS contrast doesn't generate widely out of gamut values.
    settings.gamutMapIn = true;
    // Advised for HDR. Optional for SDR, as it often looks better to at least partially clip out of gamut, to avoid
    // doing gamut smoothing (which we don't have, and would reduce gamut overall); anything else would run into flat gradients.
    settings.gamutMapOutAmount = 1.0;
    // Controls the split between highlights gamut mapping by desaturating or by darkening.
    // Best to 1 for HDR (full dechroma) and 0 to 0.5 for SDR (mixed).
    settings.gamutMapOutDechromaVsDarkening = 1.0;

    // Reference white level (not the user driven paper white scale).
    // Usually 100 nits (including Unreal Engine). Don't change unless you have a reason to.
    settings.whiteLevel = 100.0;

    // Mid grey in and out can also be seen as the eye adaptation level and ambient light level. They are also the contrast shadow/highlight anchors.
    // The can be both lowered or increased at the same time to change the S curve (e.g. for example to allow more visibility below mid grey at night).
    // Calculations are done relatively to D65 (ideally we should use to the temperature we think the viewer eyes have adapted to, but that's futuristic for now).
    settings.midGreyIn = 0.18;
    settings.midGreyOut = 0.18;

    // Determine how "filmic" our tonemapper is. Higher values produce a stronger "S" curve (shadow is already curved with a toe with the default settings).
    // Shadows and Highlights are split around the the mid grey input.
    // Note that these values are extremely sensitive, even a 0.1 change makes a huge difference (higher values gamut too).
    settings.shadowContrast = 1.0;
    settings.highlightContrast = 1.0;

    // Optional saturation grading (in more technical terms chroma or purity) multiplier.
    settings.shadowSaturation = 1.0;
    settings.highlightSaturation = 1.0;

    // Hue shifts ranges expressed in nits (for easiness), to fix the look independently of the peak.
    // This tonemapper takes your (relative) peak white brightness as input and remaps a target for the hue shifts.
    // The higher the peak white, the lower the hue shifts will be,
    // however, to facilitate mastering content with a specific look,
    // and to work around higher brightness not getting any hue shifts at all
    // in highlights (despite using per channel tonemapping), we pick fixed ranges (they are clamped too).
    // Set to extremely high value if you don't want any hue shifts but that's not advised as hue shifts are
    // necessary to create an image our perception accepts, especially when it comes to gradients.
    // Red->Orange->Yellow
    // Green->Yellow
    // Blue->Cyan (not purple like in per channel RGB tonemapping)
    //
    // The default values here are already picked to author content suitable for SDR and a range of HDR displays.
    settings.hueShiftsInMinNits = 100.0;
    settings.hueShiftsInMaxNits = 2000.0;
    settings.hueShiftsOutMinNits = 100.0;
    settings.hueShiftsOutMaxNits = 1000.0;

    // Controls how much highlights desaturate (les chroma/purity), as they approach the peak brightness target.
    // Desaturating highlights is necessary to produce perceptually believable gradients,
    // while also preserving information in them (e.g. once we reach the highest value of a channel,
    // we shift towards white, so we can represent even brighter values, and preserve information on the light sources).
    // From 0 to 1. Suggested left at default for consistent and pleasing results. It can optionally be tied to the peak white.
    settings.highlightDechroma = 1.0;

    // Restore the original shadow from the rgb input (with our grading applied), in case you had already tonemapped them
    // and just want to use this as display mapper (highlights compressor to display peak).
    settings.neutralShadowAmount = 0.0;

    // The short (~blue) "channel" of LMS has a massively different weight and behaviour in our perception.
    // For this reason, it shifts much more intensively than red and green.
    // This controls how much we try to correct it, preventing the blue->cyan transition from
    // sticking out both in brightness and hue, while also somewhat getting closer to the
    // typical RGB per channel blue hue shifting to purple
    // (this doesn't go purple, but lowers the amount we go in the opposite direction).
    // This could probably be fixed by using a "fake" LMS version with a different matrix
    // that gives more importance to blue, but this works great and gives full control.
    // This can also go beyond 1, the default is pre-calibrated to an empirically found value.
    settings.blueHueShiftsCorrection = 1.0;

    // Set this to true if you are sure you aren't changing any of the settings in this struct dynamically,
    // so we can skip some round conversion calculations based on the static parameters (optimizations that the compiler wouldn't make).
    // Make sure this setting itself is static!
    // Disabled by default as we can't know they'd be static.
    settings.staticSettings = false;

    // Optional AgX like inset matrix. Can be used to work around images with a heavy tint, or just to customize how the highlights hue shift.
    // Applied in the input color space in RGB.
    // The outset matrix is automatically generated from this (assuming it's static, otherwise modify the code and provide it yourself).
    settings.insetMatrix = float3x3(
        1.0, 0.0, 0.0, // RR, RG, RB
        0.0, 1.0, 0.0, // GR, GG, GB
        0.0, 0.0, 1.0  // BR, BG, BB
    );

    return settings;
}

// static const SpectreSettings tonemapSpectreLMSDefaultSettings = GetTonemapSpectreLMSDefaultSettings();

// Luma-Framework - Filippo Tarpini (Pumbo) - Gamma Studios - 2026
// SDR and HDR LMS tonemapper that applies perceptually pleasing and "realistic" hue shifts and highlights dechroma.
// As colors get brighter, red and green shift towards yellow, and blue towards cyan. This is perception accurate and gives the
// illusion of the objects being brighter than what they are (or more like, it makes our brain accept them).
// It doesn't suffer from the defects of per channel RGB tonemapping, like blue turning purple.
// Highlights are also "desaturated" naturally, the closer they are to the specified peak brightness. This is mandatory to produce
// realistic gradients that don't look flat, preserving details in highlights that would otherwise look monochromatic and loose information on the direction of the light.
// Shadows get a slight contrast boost, similar to a classic "filmic" S curve.
// 
// Due to LMS being a wide "color space", this requires some decent gamut mapping to avoid clipping out negative (or beyond peak) rgb colors in the output color space.
// 
// The look is also configurable in multiple ways.
// Hue shifts are applied consistently independently of your peak brightness, so you don't have to worry about testing
// all combinations and can "master" at a fixed peak brightness, without clamping to it.
// "Paper White" is meant to be applied after this, instead, divide the peak white by paper white before input (it's the "relative peak white").
// There's nothing that is inherently LMS specific in this tonemapper, it's possible to change the conversion matrices to make it work in any color space, including film stock emulation.
//
// Thanks to ShortFuse (RenoDX/PsychoV) for doing all the research about LMS and perception, and the hue functions.
// Thanks to Troy Sobotka (AgX) for teaching me how we perceive colors.
// Thanks to Musa for the feedback and ideas.
float3 TonemapSpectreLMS(float3 rgb, float peakWhite, SpectreSettings settings)
{
    const float3x3 outsetMatrix = Invert3x3Matrix(settings.insetMatrix);

    const float3 rgbLuminanceVec = settings.colorSpace == 0 ? BT709_2_XYZ[1] : BT2020_2_XYZ[1];

    // This would generally be optimized away if it's neutral.
    // Order is intentionally flipped to make the inset matrix easier to read.
    rgb = mul(rgb, settings.insetMatrix);

    float3 lms = (settings.colorSpace == 0) ? BT709ToLMS(rgb) : BT2020ToLMS(rgb);

    // Do saturation scaling before any input gamut mapping, so we already bring it back to a valid range.
    // We could fold it into the last "RestoreHueChromaLuminanceLMS" call, but it'd would worsen tonemap and gamut mapping quality, even it'd be "free" (plus it'd ignored "settings.neutralShadowAmount").
    // A simple lerp between LMS and its luminance isn't perceptual enough, so we use MacLeodBoynton.
    if (settings.shadowSaturation != 1.0 || settings.highlightSaturation != 1.0)
    {
        // Calculate the saturation for the current luminance level
        float luminance = dot(rgb, rgbLuminanceVec);
        float saturationLerpAlpha = sqrt(saturate(luminance / (settings.midGreyIn * 2.0))); // Sqrt to align with perception
        float saturation = lerp(settings.shadowSaturation, settings.highlightSaturation, saturationLerpAlpha);

        lms = RestoreHueChromaLuminanceLMS(lms, lms, settings.staticSettings, 0.0, 0.0, 0.0, true, true, saturation);
    }

    if (settings.gamutMapIn)
        lms = Color::CorrectOutOfRangeColor(lms, CIE1702_MB_WEIGHTS, true, false, false);
    // Usually not necessary as LMS is wider, but negative values cannot be used in any way, will cause NaNs
    lms = max(lms, 0.0);

    // Remap the hue shifts intensity (the remapping looks best when done linearly)
    float hueShiftsPeakWhite = RemapClamped(peakWhite, settings.hueShiftsInMinNits / settings.whiteLevel, settings.hueShiftsInMaxNits / settings.whiteLevel, settings.hueShiftsOutMinNits / settings.whiteLevel, settings.hueShiftsOutMaxNits / settings.whiteLevel).x;

    // The color space we convert from doesn't matter given the input is a D65 white level.
    float3 lmsPeaks = BT709ToLMS(peakWhite);
    float3 lmshueShiftsPeaks = BT709ToLMS(hueShiftsPeakWhite);

    float3 midGreyInLMS = BT709ToLMS(settings.midGreyIn);
    float3 midGreyOutLMS = BT709ToLMS(settings.midGreyOut);

    float lmsPeak = max(lms.r, max(lms.g, lms.b));
    float3 lmsRatio = lmsPeak > 0.0 ? saturate(lms / lmsPeak) : 1.0;
    float blueAmount = (1.0 - lmsRatio.r) * (1.0 - lmsRatio.g) * lmsRatio.b; // Short is mostly blue. This won't ever reach "1" but it's enough.
    // Some empirically found constants to correct blue hue shifts
    const float blueRestorationK1 = 0.1;
    const float blueRestorationK2 = 0.667;

    // Lerp between shadow and highlight contrast
    float3 contrastLerpAlpha = sqrt(saturate(lms / (midGreyInLMS * 2.0)));                                      // Sqrt to align with perception
    float3 coneResponseExponent = lerp(settings.shadowContrast, settings.highlightContrast, contrastLerpAlpha); // Shadow/Highlights contrast
    // Do blue correction for the hue shift cone response exponents
    float3 hueShiftsConeResponseExponent = coneResponseExponent * float3(1.0, 1.0, 1.0 + (saturate(settings.blueHueShiftsCorrection * blueAmount) * blueRestorationK1 * contrastLerpAlpha.b)); // Color contrast

    float3 lmsHueShifts = NakaRushton(lms, lmshueShiftsPeaks, midGreyInLMS, midGreyOutLMS, hueShiftsConeResponseExponent);

    // Optionally restore some hue on blue given that it turns to cyan too much with LMS per channel TM!
    if (settings.blueHueShiftsCorrection > 0.0)
    {
        // The higher this is, the more it fights off blue turning cyan and "brighter" with LMS per channel tonemapping
        float hueRestoreBlueInverseAmount = saturate(settings.blueHueShiftsCorrection * blueRestorationK2);
        // Restore only "blue" parts from the original hue
        lmsHueShifts = RestoreHueChromaLuminanceLMS(lms, lmsHueShifts, false, hueRestoreBlueInverseAmount * blueAmount);
    }

    // Tonemap in a way that is anchored around specific input and output points, for more control.
    float3 lmsTonemap = NakaRushton(lms, lmsPeaks, midGreyInLMS, midGreyOutLMS, coneResponseExponent);

    // Restore the custom hue shifts on high purity colors (approximate, from the original lms).
    // This only affects highlights because shadow (below pivot) would be identical between the two lms ratios.
    float maxLMS = max(lms.r, max(lms.g, lms.b));
    float minLMS = min(lms.r, min(lms.g, lms.b));
    float lmsSaturation = (maxLMS > 1e-6) ? ((maxLMS - minLMS) / maxLMS) : 0.0;
    lmsTonemap = RestoreHueChromaLuminanceLMS(lmsHueShifts, lmsTonemap, false, saturate(Square(lmsSaturation) * 2.0));

    // Optionally partially the chroma of the original LMS, to make highlights more saturated.
    // TODO: potential optimization, stay in the "MacLeodBoynton" color representation between "RestoreHueChromaLuminanceLMS" calls. Maybe output both from the function and only continue using the color representation you need after.
    if (settings.highlightDechroma != 1.0)
    {
        const bool allowChromaIncrease = true;
        const bool allowChromaDecrease = false; // Only shadow could really end up being more saturated, and given this controls highlights dechroma, we never want to decrease chroma in them
        lmsTonemap = RestoreHueChromaLuminanceLMS(lms, lmsTonemap, settings.staticSettings, 0.0, 1.0 - settings.highlightDechroma, 0.0, allowChromaIncrease, allowChromaDecrease);
    }

    // Do the original shadow restoration at the end, to avoid doing it once after each of the tonemappers, and do it with a blend instead of a branch, to avoid kinks
    // Note: if needed, force this to lerp up to actual mid grey, instead of the parametrized mid grey input.
    lmsTonemap = lerp(lmsTonemap, lms * (midGreyOutLMS / midGreyInLMS), settings.neutralShadowAmount * sqrt(1.0 - saturate(lms / midGreyInLMS)));

    rgb = (settings.colorSpace == 0) ? LMSToBT709(lmsTonemap) : LMSToBT2020(lmsTonemap);

    rgb = mul(rgb, outsetMatrix);

    // Note: the above can create negative RGB values, or values beyond peak, so we gamut map (in a simple way).
    // If this ever caused gradients to suddenly change in direction, we could apply some smoothing at the edges (e.g. "SmoothByAverageTonemap()"), but until proven otherwise, it's fine.
    if (settings.gamutMapOutAmount > 0.f)
    {
        const float3 preRGB = rgb;

        // Correct negatives:

        // Gamut clip with luminance restoration (hue shifting)
        rgb = lerp(RestoreLuminance(max(rgb, 0.0), rgb, false, rgbLuminanceVec), rgb, settings.gamutMapOutDechromaVsDarkening);

        // Correct whatever is left that had no already been gamut mapped above.
        // This can overly desaturate and skew the perceived hue.
        rgb = Color::CorrectOutOfRangeColor(rgb, rgbLuminanceVec, true, true, false);

        // Correct positives:

        // Partially compress by max channel. This is hue preserving but flattens gradients, so generally can't be done at full intensity.
        float rgbPeak = max3(rgb);
        rgb *= lerp(min(peakWhite / rgbPeak, 1.0), 1.0, settings.gamutMapOutDechromaVsDarkening);

        // Correct whatever is left that had no already been gamut mapped by max channel.
        // This can overly desaturate and skew the perceived hue.
        rgb = Color::CorrectOutOfRangeColor(rgb, rgbLuminanceVec, false, false, true, peakWhite);

        rgb = lerp(preRGB, rgb, settings.gamutMapOutAmount);
    }

    return rgb;
}

// Spectre end