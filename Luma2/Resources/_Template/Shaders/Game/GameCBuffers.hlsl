#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

// Game cbuffer structs shared between HLSL and C++ (32-bit scalars/vectors only).
// Replace dummy fields with the game's own (HLSL does not allow empty structs).
namespace CB
{
	struct LumaGameSettings
	{
		float dummy;
		// float example_slider;
		// float example_toggle;
		// float example_mode;
	};

	struct LumaGameData
	{
		float dummy;
		// float4 RenderResolution; // x, y, 1/x, 1/y
		// float4 OutputResolution;
		// float2 ResolutionScale; // Scale, InvScale
	};
}

#endif // LUMA_GAME_CB_STRUCTS
