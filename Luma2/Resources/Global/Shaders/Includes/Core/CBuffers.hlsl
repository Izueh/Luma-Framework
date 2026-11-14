#ifndef SRC_CBUFFERS_HLSL
#define SRC_CBUFFERS_HLSL

/////////////////////////////////////////
// The structs of Luma's cbuffers ("LumaSettings" in "Settings.hlsl", "LumaData" in "Common.hlsl"), each with the game's part.
// c++ compiles this file too (Luma's core, with the game's "Game/GameCBuffers.hlsl" before it), so both sides share one layout:
// keep it valid c++ (no semantics, no hlsl only keywords) and keep what is before a game's part a multiple of 16 bytes,
// as hlsl starts a struct on a new register and c++ doesn't.
/////////////////////////////////////////

// In case the per game code had not defined a custom struct, define a generic empty one. This behaviour is matched in c++.
#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS
#if REQUIRES_LUMA_GAME_CB_STRUCTS
#error "CBuffers.hlsl (Settings.hlsl) has not been included after the per game CB structs definitions"
#endif
namespace CB
{
	// hlsl doesn't support empty structs, so add a dummy variable (ideally it'd be empty or optional but it won't realistically affect performance)
	struct LumaGameSettings
	{
		float Dummy;
	};
	struct LumaGameData
	{
		float Dummy;
	};
}
#endif

namespace CB
{
  // Luma global settings (usually, but not necessarily, changed a maximum of once per frame)
  struct LumaGlobalSettings
  {
    float2 SwapchainSize;
    float2 SwapchainInvSize;
    float2 RenderSize;
    float2 RenderInvSize;
    // 0 for SDR (80 nits) (gamma sRGB output)
    // 1 for HDR
    // 2 for SDR on HDR (203 nits) (gamma 2.2 output)
    uint DisplayMode;
    float PeakWhiteNits; // Access this through the global variables below
    float GamePaperWhiteNits; // Access this through the global variables below (this either applies to the game scene colors, or to the whole final image)
    float UIPaperWhiteNits; // Access this through the global variables below (only usable in certain "UI_DRAW_TYPE" modes)
    uint SRType; // Which SR is enabled if any (implies it'd be engaged and compatible) (this is on even in fullscreen UI menus that don't use upscaling)
    uint FrameIndex; // Frame counter, no need for this to be by device or swapchain

// These ideally should be after anything else or if we toggled the "DEVELOPMENT" flag, the rest would get misaligned,
// so they keep what's before the game settings a multiple of 16 bytes either way.
#if DEVELOPMENT || (defined(CPU_DEVELOPMENT) && CPU_DEVELOPMENT)
    // These are reflected in ImGui (the number of them is hardcoded in c++).
    // You can add up to 3 numbers as comment to their right to define the UI settings sliders default, min and max values, and their name.
    // Like: float DevSetting01; // DefaultValue, MinValue, MaxValue, Name
    float DevSetting01; // 0, 0, 1
    float DevSetting02; // 0, 0, 1
    float DevSetting03; // 0, 0, 1
    float DevSetting04; // 0, 0, 1
    float DevSetting05; // 0, 0, 1
    float DevSetting06; // 0, 0, 1
    float DevSetting07; // 0, 0, 1
    float DevSetting08; // 0, 0, 1
    float DevSetting09; // 0, 0, 1
    float DevSetting10; // 0, 0, 1
#else
    float2 Padding1;
#endif

    // NOTE: keep anything before the game settings aligned to 16 bytes (4 floats/ints). This is the only way to match both c++ and hlsl quirks at the same time.

    // This is here to avoid taking too many cbuffer slots for separate buffers. It's at the end to avoid alignment issues with non game specific data in case the game specific includes were missing.
    CB::LumaGameSettings GameSettings; // Custom games setting, with a per game struct.
  };

  // Luma per pass/instance data, this can be customized and sent at any time
  // GPU has "32 32 32 32 | break" bits alignment on memory, so to not break any "float2", we need all the float/uint/int before them to be in groups of 2 (because we are using a unified struct).
  struct LumaInstanceData
  {
    // These can be used as non generic (pass specific) data (even a float through asfloat())
    uint CustomData1;
    uint CustomData2;
    float CustomData3;
    float CustomData4;

    float2 RenderResolutionScale;
    // This can be used instead of "CV_ScreenSize" in passes where "CV_ScreenSize" would have been
    // replaced with 1 because DLSS SR upscaled the image earlier in the rendering.
    float2 PreviousRenderResolutionScale;
    // 1 when the current pass is in the scaling zone (SR upscaled early, pass still at render-res).
    uint RenderScaleActive;

    // Make sure the game data below is aligned to 4x 32bit variables (where hlsl puts it anyway)
    float3 Padding1;

    CB::LumaGameData GameData;
  };
}

#endif // SRC_CBUFFERS_HLSL
