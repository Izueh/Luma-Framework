#include "Includes/Common.hlsl"

cbuffer CB_MOTION_VECTOR : register(b0)
{
	float4x4 reprojectionMatrix;
}

Texture2D<float4> g_velocityTex : register(t0);
Texture2D<float> g_depthTex : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.RenderSize)))
	{
		return;
	}

	float2 velocity = g_velocityTex[tid].xy;
	velocity *= 0.5f;
	velocity.x *= -1.0f;
	
	float depth = g_depthTex[tid];
	if(any(velocity < -100.0f))
	{
		float2 texCoord = (float2(tid.xy) + 0.5f) / LumaSettings.RenderSize;
		float4 prevTS = mul(float4(texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f), reprojectionMatrix);
		prevTS /= prevTS.w;
		prevTS.xy = (prevTS.xy + float2(1.0f, -1.0f)) * float2(0.5f, -0.5f);
		velocity = prevTS.xy - texCoord;
	}
	
	g_updatedVelocityTex[tid] = velocity * LumaSettings.RenderSize;
}