// Example custom Luma shader (bound via Game::D3D11::BindLumaShader).
// Remove the leading "// " below to enable compilation:
// // Luma: ps_5_0
#include "Game/Common.hlsl"

Texture2D<float4> source : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target0
{
	float4 color = source.Load(int3(pos.xy, 0));
	color.rgb *= LumaSettings.GameSettings.dummy;
	return color;
}
