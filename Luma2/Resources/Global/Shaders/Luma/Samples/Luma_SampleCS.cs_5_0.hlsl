RWTexture2D<float4> sourceTargetTexture : register(u0);

// This should be used with "command_list->Dispatch((output_texture_width + 7) / 8, (output_texture_height + 7) / 8, 1)" in DX11 c++
[numthreads(8,8,1)]
void main(uint3 vDispatchThreadId : SV_DispatchThreadID)
{
	const uint3 pixelPos = vDispatchThreadId;
	const float2 uv = pixelPos + 0.5; // Divide by the texture x y size if needed to be normalized

	uint width, height;
	sourceTargetTexture.GetDimensions(width, height);
	if (pixelPos.x >= width || pixelPos.y >= height)
		return;

	float4 color = sourceTargetTexture[pixelPos.xy];
	
	sourceTargetTexture[uint2(pixelPos.xy)] = color;
}