#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

// A stand-in game's parts of the Luma cbuffers: Tests compiles Core like a game, with these (its folder on the include path)

namespace CB
{
	struct LumaGameSettings
	{
		float Strength;
		uint Mode;
		float2 Center;
		float4 Tint;
		float Last;
	};

	struct LumaGameData
	{
		float4 Resolution;
		uint2 Size;
		float Value;
		uint RenderWidth;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
