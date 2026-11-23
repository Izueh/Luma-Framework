#ifndef SRC_SETTINGS_HLSL
#define SRC_SETTINGS_HLSL

/////////////////////////////////////////
// LUMA advanced settings
// 
// Note that the defaults might be mirrored in c++, the shader values will be overridden anyway.
// Include the game cbuffer structs define before this ("LUMA_GAME_CB_STRUCTS") if you have one.
/////////////////////////////////////////

// Composition pipeline parameters live in LumaSettings.DisplayComposition (runtime cbuffer fields) and are aliased below
// after cbuffer LumaSettings is declared so shared includes and game shaders can read them directly.
//
// Whether we correct gamma even on colors beyond the 0-1 range.
// 0 Whole range, 1 0-1 range only
#ifndef GAMMA_CORRECTION_RANGE_TYPE
#define GAMMA_CORRECTION_RANGE_TYPE 1
#endif
// Higher quality gamma<->linear conversions, it avoids the error generated from the conversion by restoring the change on the original color in an additive way.
// This has a relatively high performance cost for the visual gains it returns.
#define HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS 1
// Necessary for HDR to work correctly
#ifndef ENABLE_LUT_EXTRAPOLATION
#define ENABLE_LUT_EXTRAPOLATION 1
#endif
// See "LUTExtrapolationSettings::extrapolationQuality"
#ifndef LUT_EXTRAPOLATION_QUALITY
#define LUT_EXTRAPOLATION_QUALITY 1
#endif
// It's better to leave the classic LUT interpolation (bilinear/trilinear),
// LUTs in Prey are very close to being neutral so tetrahedral interpolation just shifts their colors without gaining much, possibly actually losing quality.
// This is even less necessary when LUTs input and output is linear.
#ifndef ENABLE_LUT_TETRAHEDRAL_INTERPOLATION
#define ENABLE_LUT_TETRAHEDRAL_INTERPOLATION 0
#endif
// Do it higher than 8 bit for HDR
#ifndef DITHERING_BIT_DEPTH
#define DITHERING_BIT_DEPTH 9u
#endif
// Disables development features if off
#ifndef DEVELOPMENT
#define DEVELOPMENT 0
#endif
#ifndef TEST
#define TEST 0
#endif
#ifndef LUMA_SETTINGS_CB_INDEX
#define LUMA_SETTINGS_CB_INDEX b13
#endif
#ifndef LUMA_DATA_CB_INDEX
#define LUMA_DATA_CB_INDEX b12
#endif
#ifndef LUMA_UI_DATA_CB_INDEX
#define LUMA_UI_DATA_CB_INDEX b11
#endif
#ifndef LUMA_COMPOSITION_CB_INDEX
#define LUMA_COMPOSITION_CB_INDEX b10
#endif

/////////////////////////////////////////
// Rendering features toggles (development)
/////////////////////////////////////////

// This might also disable decals interfaces (like computer screens) in the 3D scene
#define ENABLE_UI (!DEVELOPMENT || 1)

/////////////////////////////////////////
// Debug toggles
/////////////////////////////////////////

// 0 None
// 1 Neutral LUT
// 2 Neutral LUT + bypass extrapolation
#if !defined(FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE) || !DEVELOPMENT
#undef FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE
#define FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE (DEVELOPMENT && 0)
#endif
#if !defined(DRAW_LUT) || !DEVELOPMENT
#undef DRAW_LUT
#define DRAW_LUT (DEVELOPMENT && 0)
#endif
// Debug LUT Pixel scale (this is rounded to the closest integer value for the size of the LUT)
// 10u is a good value for 2560 horizontal res. 20 for 5120 horizontal res or more.
#define DRAW_LUT_TEXTURE_SCALE 10u

/////////////////////////////////////////
// LUMA user settings
/////////////////////////////////////////

// The game's cbuffer structs ("LUMA_GAME_CB_STRUCTS") come before this, or "CBuffers.hlsl" defines empty ones
#include "Includes/Core/CBuffers.hlsl"

// Luma global settings (usually, but not necessarily, changed a maximum of once per frame), "CB::LumaGlobalSettings" in "CBuffers.hlsl"
// Regarding "LUMA_SETTINGS_CB_INDEX", most engines (e.g. CryEngine, Unreal, Unity) push the registers that are used by each shader again for every draw, so it's generally safe to overridden them anyway (they are all reset between frames).
// Game and Dev settings should only be accessed outside of global shaders and includes, given the struct isn't guaranteed to be fully defined there.
cbuffer LumaSettings : register(LUMA_SETTINGS_CB_INDEX)
{
  CB::LumaGlobalSettings LumaSettings : packoffset(c0);
}

#define POST_PROCESS_SPACE_TYPE (LumaSettings.DisplayComposition.PostProcessSpaceType)
#define VANILLA_ENCODING_TYPE (LumaSettings.DisplayComposition.VanillaEncodingType)
#define GAMMA_CORRECTION_TYPE (LumaSettings.DisplayComposition.GammaCorrectionType)
#define GAMUT_MAPPING_TYPE (LumaSettings.DisplayComposition.GamutMappingType)
#define UI_DRAW_TYPE (LumaSettings.DisplayComposition.UIDrawType)
#define EARLY_DISPLAY_ENCODING (LumaSettings.DisplayComposition.EarlyDisplayEncoding)
#if DEVELOPMENT
#define TEST_2X_ZOOM ((LumaSettings.DisplayComposition.DebugFlags & 1u) != 0u)
#define TEST_SDR_HDR_SPLIT_VIEW_MODE ((LumaSettings.DisplayComposition.DebugFlags >> 1u) & 7u)
#define TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL ((LumaSettings.DisplayComposition.DebugFlags & 16u) != 0u)
#else
#define TEST_2X_ZOOM 0
#define TEST_SDR_HDR_SPLIT_VIEW_MODE 0
#define TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL 0
#endif

// These parameters are already pushed directly from c++ so we don't need to check them manually anymore
#if 1
bool ShouldForceWhiteLevel() { return false; }
#else
bool ShouldForceWhiteLevel() { return LumaSettings.DisplayMode == 0; }
#endif
float GetForcedWhileLevel() { return (LumaSettings.DisplayMode == 0) ? sRGB_WhiteLevelNits : ITU_WhiteLevelNits; }

#if DEVELOPMENT
#define DVS1 LumaSettings.DevSetting01
#define DVS2 LumaSettings.DevSetting02
#define DVS3 LumaSettings.DevSetting03
#define DVS4 LumaSettings.DevSetting04
#define DVS5 LumaSettings.DevSetting05
#define DVS6 LumaSettings.DevSetting06
#define DVS7 LumaSettings.DevSetting07
#define DVS8 LumaSettings.DevSetting08
#define DVS9 LumaSettings.DevSetting09
#define DVS10 LumaSettings.DevSetting10
#else // Default to 1 even if when enabled they default to 0, it's more likely to be the neutral value
#define DVS1 1.0
#define DVS2 1.0
#define DVS3 1.0
#define DVS4 1.0
#define DVS5 1.0
#define DVS6 1.0
#define DVS7 1.0
#define DVS8 1.0
#define DVS9 1.0
#define DVS10 1.0
#endif

#ifdef HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS // You can define this to force an hardcoded paper white (for whatever reason)
static const float GamePaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
static const float UIPaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
#elif DEVELOPMENT
static const float GamePaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.GamePaperWhiteNits != 0 ? LumaSettings.GamePaperWhiteNits : ITU_WhiteLevelNits);
static const float UIPaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.UIPaperWhiteNits != 0 ? LumaSettings.UIPaperWhiteNits : ITU_WhiteLevelNits);
#else // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
static const float GamePaperWhiteNits = LumaSettings.GamePaperWhiteNits;
static const float UIPaperWhiteNits = LumaSettings.UIPaperWhiteNits;
#endif // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
#ifdef HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = HDR_TONEMAP_PEAK_BRIGHTNESS;
#elif DEVELOPMENT
static const float PeakWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.PeakWhiteNits != 0 ? LumaSettings.PeakWhiteNits : 1000.0); // Same peak white default as in c++
#else // HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = LumaSettings.PeakWhiteNits;
#endif // HDR_TONEMAP_PEAK_BRIGHTNESS

#endif // SRC_SETTINGS_HLSL