#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// Prey LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

// The LUMA mod changed LUTs textures from UNORM 8 bit to FP 16 bit, so their sRGB (scRGB) values can theoretically go negative to conserve HDR colors
#define ENABLE_HDR_COLOR_GRADING_LUT 1
// We changed LUT format to R16G16B16A16F so in their mixing shaders, we store them in linear space, for higher output quality and to keep output values beyond 1 (their input coordinates are still in gamma sRGB (and then need 2.2 gamma correction)).
// This has a relatively small performance cost.
#ifndef ENABLE_LINEAR_COLOR_GRADING_LUT
#define ENABLE_LINEAR_COLOR_GRADING_LUT 1
#endif
// 0 Vanilla SDR (Hable), 1 Luma HDR (Vanilla+ Hable/DICE mix) (also works for SDR), 2 Untonemapped
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif
// Better kept to true for sun shafts and lens optics and other post process effects, and AA too.
// Note that this will theoretically apply tonemap on object highlights too, but they are additive and were clipped in SDR, plus we don't really tonemap until beyond 203 nits, so it's ok.
#ifndef TRY_DELAY_HDR_TONEMAP
#define TRY_DELAY_HDR_TONEMAP 1
#endif
#define DELAY_HDR_TONEMAP (TRY_DELAY_HDR_TONEMAP && TONEMAP_TYPE == 1)
// Sun shafts were drawn after tonemapping in the Vanilla game, thus they were completely SDR, Luma has implemented an HDR version of them which tries to retain the artistic direction.
// This looks better as true in SDR too, as it avoids heavy clipping.
#define ANTICIPATE_SUNSHAFTS (!DELAY_HDR_TONEMAP || 1)
// 0 Vanilla
// 1 Medium (Vanilla+)
// 2 High
// 3 Extreme (worst performance)
#define SUNSHAFTS_QUALITY 2
// 0 Raw Vanilla: raw sun shafts values, theoretically close to vanilla but in reality not always, it might not look good
// 1 Vanilla+: similar to vanilla but HDR and tweaked to be even closer (it's more "realistic" than SDR vanilla)
// 2 LUMA HDR: a bit dimmer but more realistic, so it works best with "ENABLE_LENS_OPTICS_HDR", which compensates for the lower brightness/area
#ifndef SUNSHAFTS_LOOK_TYPE
#define SUNSHAFTS_LOOK_TYPE 2
#endif
// Unjitter the sun shafts depth buffer and re-jitter their generation.
// This is because they draw before TAA/DLSS but with screen space logic, so jittering needs to be done manually.
#define REJITTER_SUNSHAFTS 1
// Some lens optics effects (and maybe sun shafts?) did not acknowledge FOV (they drew in screen space, independently of FOV),
// so if you zoomed in, you'd get "smaller" (they'd be the same size in screen space, thus smaller relative to the rest).
// This would theoretically change the intended size of these effects during cutscenes if they changed the FOV from the gameplay one,
// but there really aren't any in Prey.
#define CORRECT_SUNSHAFTS_FOV 1
// Lens optics were clipped to 1 due to being rendered before tonemapping. As long as "DELAY_HDR_TONEMAP" is true, now these will also be tonemapped instead of clipped (even in SDR, so "TONEMAP_TYPE" needs to be HDR).
#if !defined(ENABLE_LENS_OPTICS_HDR) || ENABLE_LENS_OPTICS_HDR >= 1
#undef ENABLE_LENS_OPTICS_HDR
#define ENABLE_LENS_OPTICS_HDR (TONEMAP_TYPE >= 1)
#endif
#ifndef AUTO_HDR_VIDEOS
#define AUTO_HDR_VIDEOS 1
#endif
//TODOFT6: pick default value (in c++ too)
#ifndef EXPAND_COLOR_GAMUT
#define EXPAND_COLOR_GAMUT 1
#endif
#define DELAY_DITHERING 1
//TODOFT: test more with this off (which theoretically should be better), and possibly disable it (or remove it if you move the AA pass)
#ifndef SR_RELATIVE_PRE_EXPOSURE
#define SR_RELATIVE_PRE_EXPOSURE 1
#endif
// Disable to keep the vanilla behaviour of CRT like emulated effects becoming near imperceptible at higher resolutions (which defeats their purpose)
#ifndef CORRECT_CRT_INTERLACING_SIZE
#define CORRECT_CRT_INTERLACING_SIZE 1
#endif
// Disable to force lens distortion to crop all black borders (further increasing FOV is suggested if you turn this off)
#ifndef ALLOW_LENS_DISTORTION_BLACK_BORDERS
#define ALLOW_LENS_DISTORTION_BLACK_BORDERS 1
#endif
// If true, the motion vectors generated for dynamic objects are generated with both the current and previous jitters acknowledged in the calculations (and baked in their velocity, so they wouldn't be zero even if nothing was moving).
// If false, motion vectors are generated (and then interpreted in Motion Blur and TAA) like in the vanilla code, so they kinda include the jitter of the current frame, but not the one from the previous frame, which isn't really great and caused micro shimmers in blur and TAA.
// This needs to be mirrored in c++ so do not change it directly from here. In post process shaders it simply determines how to interpret/dejitter the MVs. When DLSS is on, the behaviour is always is if this was true.
#ifndef FORCE_MOTION_VECTORS_JITTERED
#define FORCE_MOTION_VECTORS_JITTERED 1
#endif
// Allows to disable this given it might not be liked (it can't be turned off individually) and can make DLSS worse. This needs "r_MotionBlurCameraMotionScale" to not be zero too (it's not by default).
#ifndef ENABLE_CAMERA_MOTION_BLUR
#define ENABLE_CAMERA_MOTION_BLUR 0
#endif
// 0 SSDO (Vanilla, CryEngine)
// 1 GTAO (Luma)
#ifndef SSAO_TYPE
#define SSAO_TYPE 1
#endif
// 0 Vanilla
// 1 High (best balance for 2024 GPUs)
// 2 Extreme (bad performance)
#ifndef SSAO_QUALITY
#define SSAO_QUALITY 1
#endif
// 0 Small (makes the screen space limitations less apparent)
// 1 Vanilla
// 2 Large (can look more realistic, but also over darkening and bring out the screen space limitations (e.g. stuff de-occluding around the edges when turning the camera))
// GTAO only
#ifndef SSAO_RADIUS
#define SSAO_RADIUS 1
#endif
// Makes AO jitter a bit to add blend in more quality over time.
// Requires TAA enabled to not look terrible.
#ifndef ENABLE_SSAO_TEMPORAL
#define ENABLE_SSAO_TEMPORAL 1
#endif
// 0 Vanilla
// 1 High
#ifndef BLOOM_QUALITY
#define BLOOM_QUALITY 1
#endif
// Dejitter and rejitter bloom (and exposure) generation.
// it makes bloom a lot more stable with TAA, but slightly changes its intensity.
#define REJITTER_BLOOM 1
// 0 Vanilla (based on user setting)
// 1 Ultra
#ifndef MOTION_BLUR_QUALITY
#define MOTION_BLUR_QUALITY 1
#endif
// 0 Vanilla
// 1 High (best balance)
// 2 Ultra (slow)
// 3 Extreme (slowest)
#ifndef SSR_QUALITY
#define SSR_QUALITY 1
#endif
// 0 None: disabled (soft)
// 1 Vanilla: basic sharpening
// 2 RCAS: AMD improved sharpening (default preset)
// 3 RCAS: AMD improved sharpening (strong preset)
#ifndef POST_TAA_SHARPENING_TYPE
#define POST_TAA_SHARPENING_TYPE 2
#endif
// Disabled as we are now in HDR (10 or 16 bits)
#ifndef ENABLE_DITHERING
#define ENABLE_DITHERING 0
#endif

//TODOFT2: try to boost the chrominance on highlights? Or desaturate, the opposite.
//TODOFT3: lower mid tones to boost highlights? Nah
//TODOFT: add viewport print debug, but it seems like everything uses full viewport
//TODOFT0: disable all dev/debug settings below, even for dev mode
//TODOFT: add test setting to disable all exposure, and see if the game looks more "HDR" (though tonemapping would break...?)
//TODOFT0: fix formatting/spacing of all shaders
//TODOFT: test reflections mips flickering or disappearing? And glass flickering?
//TODOFT4: review "D3D11 ERROR: ID3D11DeviceContext::Dispatch: The resource return type for component 0 declared in the shader code (FLOAT) is not compatible with the resource type bound to Unordered Access View slot 0 of the Compute Shader unit (UNORM). This mismatch is invalid if the shader actually uses the view (e.g. it is not skipped due to shader code branching). [ EXECUTION ERROR #2097372: DEVICE_UNORDEREDACCESSVIEW_RETURN_TYPE_MISMATCH]"

/////////////////////////////////////////
// Rendering features toggles (development)
/////////////////////////////////////////

#ifndef ENABLE_POST_PROCESS
#define ENABLE_POST_PROCESS 1
#endif
// The game already has a setting for this
#define ENABLE_MOTION_BLUR (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#define ENABLE_BLOOM (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// The game already has a setting for this
#define ENABLE_SSAO (!DEVELOPMENT || 1)
// Spacial (not temporal) SSAO denoising. Needs to be enabled for it to look good.
#define ENABLE_SSAO_DENOISE (!DEVELOPMENT || 1)
// Disables all kinds of AA (SMAA, FXAA, TAA, ...) (disabling "ENABLE_SHARPENING" is also suggested if disabling AA). Doesn't affect DLSS.
#define ENABLE_AA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Optional SMAA pass being run before TAA
#define ENABLE_SMAA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Optional TAA pass being run after the optional SMAA pass
#define ENABLE_TAA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#if !defined(ENABLE_COLOR_GRADING_LUT) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_COLOR_GRADING_LUT
#define ENABLE_COLOR_GRADING_LUT (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
// Note that this only disables the tonemap step sun shafts, not the secondary ones from lens optics
#define ENABLE_SUNSHAFTS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Note that these might ignore "ShouldSkipPostProcess()"
#define ENABLE_ARK_CUSTOM_POST_PROCESS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#define ENABLE_LENS_OPTICS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Disable this for a softer image
// (not really needed anymore as now we have "POST_TAA_SHARPENING_TYPE" for the TAA sharpening, which is the only one that usually runs)
#if !defined(ENABLE_SHARPENING) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_SHARPENING
#define ENABLE_SHARPENING (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
#define ENABLE_CHROMATIC_ABERRATION (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Lens distortion and such
#define ENABLE_SCREEN_DISTORTION (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#if !defined(ENABLE_VIGNETTE) || !ENABLE_POST_PROCESS
#undef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE (ENABLE_POST_PROCESS && 1)
#endif
// This is used for gameplay effects too, so it's best not disabled
#if !defined(ENABLE_FILM_GRAIN) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_FILM_GRAIN
#define ENABLE_FILM_GRAIN (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
// Note: when disabling this, exposure can go to 0 or +INF when the game is paused somehow
#define ENABLE_EXPOSURE_CLAMPING (!DEVELOPMENT || 1)

/////////////////////////////////////////
// Debug toggles
/////////////////////////////////////////

// Test extra saturation to see if it passes through (HDR colors)
#define TEST_HIGH_SATURATION_GAMUT (DEVELOPMENT && 0)
#define TEST_TONEMAP_OUTPUT (DEVELOPMENT && 0)
#define TEST_LUT_EXTRAPOLATION (DEVELOPMENT && 0)
#define TEST_LUT (DEVELOPMENT && 1)
#define TEST_TINT (DEVELOPMENT && 1)
// Tests some alpha blends stuff
#define TEST_UI (DEVELOPMENT && 1)
// 0 None
// 1 Motion Blur Motion Vectors
// 2 Motion Vectors discard check
// 3 Motion Vectors length
#define TEST_MOTION_BLUR_TYPE (DEVELOPMENT ? 0 : 0)
// 0 None
// 1 Jitters
// 2 Depth Buffer
// 3 Reprojection Matrix
// 4 Motion Vectors (of dynamic geometry that moves in world space, not relatively to the camera)
// 5 Force blending with the previous frame to test temporal stability
#define TEST_TAA_TYPE (DEVELOPMENT ? 0 : 0)
// 0 None
// 1 Additive Bloom
// 2 Native Bloom
#define TEST_BLOOM_TYPE (DEVELOPMENT ? 0 : 0)
#define TEST_SUNSHAFTS (DEVELOPMENT && 0)
// 0 None
// 1 Show fixed color
// 2 Show only lens optics
#define TEST_LENS_OPTICS_TYPE (DEVELOPMENT ? 0 : 0)
#define TEST_DITHERING (DEVELOPMENT && 0)
#define TEST_SMAA_EDGES (DEVELOPMENT && 0)
#define TEST_DYNAMIC_RESOLUTION_SCALING (DEVELOPMENT && 0)
#define TEST_EXPOSURE (DEVELOPMENT && 0)
#define TEST_SSAO (DEVELOPMENT && 0)

// For best results, we should consider the FOV Hor+ beyond 16:9 and Vert- below 16:9
// (so the 16:9 image is always visible, and the aspect ratio either extends the vertical or horizontal view).
// Default value used all across the game for gameplay (and possibly during cutscenes too)
static const float NativeVerticalFOV = radians(55.0);
// The vertical resolution that most likely was the most used by the game developers,
// we define this to scale up stuff that did not natively correctly scale by resolution.
// According to the developers, the game was mostly developed on 1080p displays, and some 1440p ones, so
// we are going for their middle point, but 1080 or 1440 would also work fine.
static const float BaseVerticalResolution = 1260.0;
static const float BaseHorizontalResolution = BaseVerticalResolution * 16.0 / 9.0;

// Exposure multiplier for sunshafts. It's useful to shift them towards a better range for float textures to avoid banding.
// This comes from vanilla values, it's not really meant to be changed.
static const float SunShaftsBrightnessMultiplier = 4.0;
// With "SUNSHAFTS_LOOK_TYPE" > 0 and "ENABLE_LENS_OPTICS_HDR", we apply exposure to sun shafts and lens optics as well.
// Given that exposure can deviate a lot from a value of 1, to the point where it would make lens optics effects look weird, we diminish its effect on them so it's less jarring, but still applies (which is visually nicer).
// The value should be between 0 and 1.
static const float SunShaftsAndLensOpticsExposureAlpha = 0.25; // Anything more than 0.25 can cause sun effects to be blinding if the exposure is too high (it's pretty high in some scenes)

static const float BinkVideosAutoHDRPeakWhiteNits = 250; // Values beyond 700 will make AutoHDR look bad
// The higher it is, the "later" highlights start
static const float BinkVideosAutoHDRShoulderPow = 2.75; // A somewhat conservative value

#endif // SRC_GAME_SETTINGS_HLSL