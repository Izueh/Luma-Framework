#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
   float4 screenDims;
   float4 blurCoCOffsets[5];
   float4 maxCoCOffsets[16];
}

void main(float4 position : POSITION0, float2 uv : TEXCOORD0,
   out float4 outputPosition : SV_POSITION, out float4 uv1 : TEXCOORD1,
   out float4 uv2 : TEXCOORD2, out float4 uv3 : TEXCOORD3)
{
   float2 scale = LumaData.GameData.IsUpscaling != 0 ? LumaData.RenderResolutionScale.xy : float2(1.0, 1.0);
   outputPosition = position;
   uv1 = float4(0.0, 0.0, uv + blurCoCOffsets[0].xy * scale);
   uv2 = float4(uv + blurCoCOffsets[1].xy * scale, uv + blurCoCOffsets[2].xy * scale);
   uv3 = float4(uv + blurCoCOffsets[3].xy * scale, uv + blurCoCOffsets[4].xy * scale);
}
