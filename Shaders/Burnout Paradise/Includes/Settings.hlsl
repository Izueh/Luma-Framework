#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// Burnout Paradise Remastered LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

#if !defined(ENABLE_LUMA)
#define ENABLE_LUMA 1
#endif

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

#ifndef ENABLE_IMPROVED_MOTION_BLUR
#define ENABLE_IMPROVED_MOTION_BLUR 1
#endif

#ifndef ENABLE_IMPROVED_BLOOM
#define ENABLE_IMPROVED_BLOOM 1
#endif

#ifndef LUT_SAMPLING_ERROR_EMULATION_MODE
#define LUT_SAMPLING_ERROR_EMULATION_MODE 1
#endif

#ifndef REMOVE_BLACK_BARS
#define REMOVE_BLACK_BARS 0
#endif

#endif // SRC_GAME_SETTINGS_HLSL