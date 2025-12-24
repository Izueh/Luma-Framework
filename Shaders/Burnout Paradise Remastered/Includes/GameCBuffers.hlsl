#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
		float BloomIntensity; // Neutral/Vanilla at 1
		float MotionBlurIntensity; // Neutral/Vanilla at 1
		float ColorGradingIntensity; // Neutral/Vanilla at 1
		float ColorGradingFilterReductionIntensity; // Neutral/Vanilla at 0
		float HDRBoostIntensity; // Neutral/Vanilla at 0
		float OriginalTonemapperColorIntensity; // Neutral/Vanilla at ~1 (it won't look the same, it's still the HDR path)
		float2 InvRenderRes;
		float Padding1; // Align to 16 bytes (somehow needed...)
	};
	
	struct LumaGameData
	{
		float Dummy;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
