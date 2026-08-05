#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   float4 RenderResolution; // xy = dimensions, zw = 1/dimensions
   float tonemap_type;
   float BloomStrength;
   float Sharpness;
};

struct LumaGameData
{
   int IsUpscaling;
};
}

#endif // LUMA_GAME_CB_STRUCTS
