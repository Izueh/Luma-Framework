#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// Final Fantasy VII Remake LUMA advanced settings
/////////////////////////////////////////

// 0 SSDO (Vanilla, UE4)
// 1 GTAO (Luma)
#ifndef SSAO_TYPE
#define SSAO_TYPE 1
#endif

// 0 Low
// 1 Medium
// 2 High (default)
// 3 Very High
// 4 Ultra
#ifndef XE_GTAO_QUALITY
#define XE_GTAO_QUALITY 2
#endif

// 0 Normal denoise output
// 1 Debug raw AO visualization
#ifndef XE_GTAO_DEBUG_OUTPUT
#define XE_GTAO_DEBUG_OUTPUT 1
#endif

#endif // SRC_GAME_SETTINGS_HLSL