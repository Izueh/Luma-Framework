#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
		float Dummy;
	};

	struct LumaGameData
	{
		float2 JitterOffset;     // Current frame jitter to apply (in NDC, _m20/_m21 space)
		float2 PrevJitterOffset; // Previous frame jitter (for g_PrevProj)
	};
}

#endif // LUMA_GAME_CB_STRUCTS
