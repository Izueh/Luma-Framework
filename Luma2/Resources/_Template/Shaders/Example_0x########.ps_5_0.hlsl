// Example shader replacement: rename to "<Name>_0x<HASH>.<target>.hlsl" (or call Shaders::RedirectHashes).
#include "Game/Common.hlsl"
// D3D12 with `.bindings = {.static_samplers = true}`: Luma's static samplers (LUMA_HAS_STATIC_SAMPLERS)
#include "Includes/Core/Samplers.hlsl"

// ### Replace with the game's decompiled shader ###
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
   return float4(uv, LumaSettings.GameSettings.dummy, 1.0);
}
