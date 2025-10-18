#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
    {
        uint4 _dummy;
	};

	struct LumaGameData
	{
        float4 RenderResolution;
		float4 ViewportRect;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
