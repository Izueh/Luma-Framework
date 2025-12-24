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

#ifndef ENABLE_DOF
#define ENABLE_DOF 1
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
#define LUT_SAMPLING_ERROR_EMULATION_MODE 0
#endif

#ifndef REMOVE_BLACK_BARS
#define REMOVE_BLACK_BARS 0
#endif

#ifndef SMOOTH_MOTION_BLUR
#define SMOOTH_MOTION_BLUR 1
#endif

#ifndef FORWARDS_ONLY_MOTION_BLUR
#define FORWARDS_ONLY_MOTION_BLUR 0
#endif

#ifndef REDUCE_HORIZONTAL_MOTION_BLUR
#define REDUCE_HORIZONTAL_MOTION_BLUR 1
#endif

#ifndef MOTION_BLUR_IMPROVE_STENCIL_FILTER
#define MOTION_BLUR_IMPROVE_STENCIL_FILTER 1
#endif

#ifndef MOTION_BLUR_BLUR_DISTANT_CARS
#define MOTION_BLUR_BLUR_DISTANT_CARS 1
#endif

#endif // SRC_GAME_SETTINGS_HLSL