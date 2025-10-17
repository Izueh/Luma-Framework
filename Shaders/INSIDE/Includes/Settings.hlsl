#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// INSIDE LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

#if !defined(ENABLE_LUMA)
#define ENABLE_LUMA 1
#endif

#if !defined(ENABLE_FILM_GRAIN)
#define ENABLE_FILM_GRAIN 1
#endif

#if !defined(ENABLE_LENS_DISTORTION)
#define ENABLE_LENS_DISTORTION 1
#endif

#if !defined(ENABLE_CHROMATIC_ABERRATION)
#define ENABLE_CHROMATIC_ABERRATION 1
#endif

#if !defined(ENABLE_FAKE_HDR)
#define ENABLE_FAKE_HDR 0
#endif

#if !defined(ENABLE_BLACK_FLOOR_TWEAKS_TYPE)
#define ENABLE_BLACK_FLOOR_TWEAKS_TYPE 1
#endif

#if !defined(ENABLE_DITHER)
#define ENABLE_DITHER 1
#endif

#endif // SRC_GAME_SETTINGS_HLSL