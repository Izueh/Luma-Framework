// Luma: ps_5_0
#include "Includes/Core/Common.hlsl"
#include "Includes/Color/ColorGradingLUT.hlsl" // Use this as it has some gamma correction helpers
#include "Includes/Tonemap/Reinhard.hlsl"

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> uiTexture : register(t1); // Optional: Pre-multiplied UI
#if DEVELOPMENT
Texture2D<float4> debugTexture2D : register(t2);
Texture2DMS<float4> debugTexture2DMS : register(t3);
Texture2DArray<float4> debugTexture2DArray : register(t4);
Texture2DMSArray<float4> debugTexture2DMSArray : register(t5);
Texture3D<float4> debugTexture3D : register(t6);
Texture1D<float4> debugTexture1D : register(t7);
Texture1DArray<float4> debugTexture1DArray : register(t8);
// TODO: add TextureCube(s) support... Update "debug_draw_srv_slot_numbers" in c++ if you change this. And also for uint/sint textures.
#endif

// Note: this might not always be set, only use in known cases
SamplerState linearSampler : register(s0);

cbuffer LumaComposition : register(LUMA_COMPOSITION_CB_INDEX)
{
	CB::LumaCompositionData LumaComposition : packoffset(c0);
}

// Each stage can be switched off on its own (Display::Composition::Stage), and is an identity then
bool HasStage(uint stage) { return (LumaSettings.DisplayComposition.Stages & (1u << stage)) != 0; }
#define STAGE_DECODE 0u // The game's encoding to linear
#define STAGE_UI 1u // UI composite and its paper white
#define STAGE_GAMMA 2u // Gamma correction (sRGB and 2.2 mismatch)
#define STAGE_GAMUT 3u // Gamut mapping
#define STAGE_SCALE 4u // From the game's size to the real buffer's
#define STAGE_DEVELOPMENT 5u // Debug draw, SDR/HDR split

#if DEVELOPMENT
bool DrawDebugTexture(float3 pos, inout float4 outColor, float gamePaperWhite, float UIPaperWhite)
{
	bool _texture2D = (LumaData.CustomData2 & (1 << 13)) != 0; // "_" as the name is seemengly taken in hlsl
	bool _texture3D = (LumaData.CustomData2 & (1 << 14)) != 0;
	bool _texture1D = !_texture2D && !_texture3D; // We don't have a flag for "no texture", we simply unbind all of them
	bool _textureCube = _texture2D && _texture3D;
	bool _textureMS = (LumaData.CustomData2 & (1 << 11)) != 0;
	bool _textureArray = (LumaData.CustomData2 & (1 << 12)) != 0;
	float debugWidth = 0.0, debugHeightOrArraySize = 1.0, debugDepthOrArraySize = 1.0, sampleCount = 1.0; // Default to 0 to make sure they stay zero if the texture wasn't bound
	if (_texture1D && _textureArray)
	{
		debugTexture1DArray.GetDimensions(debugWidth, debugHeightOrArraySize);
	}
	else if (_texture1D)
	{
		debugTexture1D.GetDimensions(debugWidth);
	}
	else if (_texture3D)
	{
		debugTexture3D.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize);
	}
	else if (_textureMS && _textureArray)
	{
		debugTexture2DMSArray.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize, sampleCount);
	}
	else if (_textureMS)
	{
		debugTexture2DMS.GetDimensions(debugWidth, debugHeightOrArraySize, sampleCount);
	}
	else if (_textureArray)
	{
		debugTexture2DArray.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize);
	}
	else // _texture2D
	{
		debugTexture2D.GetDimensions(debugWidth, debugHeightOrArraySize);
	}
	// TODO: add better support for depth stencil formats? uint?
	// Skip if there's no texture. It might be undefined behaviour, but it seems to work on Nvidia
	bool drew = false;
	if (debugWidth != 0.0)
	{
		float2 resolutionScale = 1.0;
		bool fullscreen = (LumaData.CustomData2 & (1 << 0)) != 0;
		bool renderResolutionScale = (LumaData.CustomData2 & (1 << 1)) != 0;
		bool showAlpha = (LumaData.CustomData2 & (1 << 2)) != 0;
		bool premultiplyAlpha = (LumaData.CustomData2 & (1 << 3)) != 0;
		bool invertColors = (LumaData.CustomData2 & (1 << 4)) != 0;
		bool linearToGamma = (LumaData.CustomData2 & (1 << 5)) != 0;
		bool gammaToLinear = (LumaData.CustomData2 & (1 << 6)) != 0;
		bool flipY = (LumaData.CustomData2 & (1 << 7)) != 0;
		bool doAbs = (LumaData.CustomData2 & (1 << 15)) != 0;
		bool doSaturate = (LumaData.CustomData2 & (1 << 8)) != 0;
		bool redOnly = (LumaData.CustomData2 & (1 << 9)) != 0;
		bool backgroundPassthrough = (LumaData.CustomData2 & (1 << 10)) != 0;
		bool zoom4x = (LumaData.CustomData2 & (1 << 16)) != 0;
		bool bilinear = (LumaData.CustomData2 & (1 << 17)) != 0;
		bool tonemap = (LumaData.CustomData2 & (1 << 18)) != 0;
		bool sRGB = (LumaData.CustomData2 & (1 << 19)) != 0; // sRGB in/out (instead of gamma 2.2)
		bool UVToPixelSpace = (LumaData.CustomData2 & (1 << 20)) != 0;
		bool denormalize = (LumaData.CustomData2 & (1 << 21)) != 0;
		int mipLevel = LumaData.CustomData3 + 0.5; // 10 bits for the mip level
	
		float3 debugPos = float3(pos.xy, 0.5);
		int sliceWidth = debugWidth + 0.5;
		debugPos.z = (uint(debugPos.x) / sliceWidth) + 0.5; // Basically: "depthOrArrayIndex"

		float targetWidth;
		float targetHeight;
		sourceTexture.GetDimensions(targetWidth, targetHeight);

		bool size2DMatches = debugWidth == targetWidth && debugHeightOrArraySize == targetHeight;
		fullscreen = fullscreen || size2DMatches;
		
		if (fullscreen) // Stretch to fullscreen
		{
			resolutionScale = float2(debugWidth / targetWidth, debugHeightOrArraySize / targetHeight);
			if (flipY)
			{
				debugPos.y = targetHeight - debugPos.y;
			}
		}
		else
		{
			if (debugDepthOrArraySize != 1) // TODO: what is this for exactly? Some array textures?
			{
				debugPos.x = (uint(debugPos.x) % sliceWidth) + 0.5;
			}
			// TODO: handle if this works with "renderResolutionScale" (e.g. Prey)
			if (flipY)
			{
				debugPos.y = debugHeightOrArraySize - debugPos.y;
			}
		}
		if (renderResolutionScale) // Scale by rendering resolution (so to stretch the used part of the image to the full texture range) (note that this might not work so well if the game draws at a different aspect ratio and then adds black bars)
		{
			resolutionScale *= LumaData.RenderResolutionScale;
		}
		
		// Zoom around the center		
		float zoom = zoom4x ? 4.0 : 1.0;
		float3 debugSize = float3(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize); // I don't think we need -1 on this
		float3 center = fullscreen ? 0 : (-debugSize * 0.5); // Zoom around the center if non fullscreen mode // TODO: polish... not right
		debugPos.xyz = ((debugPos.xyz - center) / zoom) + center;
		// TODO: add a brightness scale to convert from UV space to pixel space (e.g. motion vectors)

		// TODO: should we also apply the "resolutionScale" around the center?
		int3 debugPosInt;
		debugPosInt.xy = round((debugPos.xy - 0.5) * resolutionScale) + 0.5;
		debugPosInt.z = debugPos.z * resolutionScale.x;

		debugPos.xy *= resolutionScale;
		debugPos.z *= resolutionScale.x;
	
		bool validTexel = debugPos.x < debugSize.x && debugPos.y < debugSize.y && debugPos.z < debugSize.z && debugPos.x >= 0.0 && debugPos.y >= 0.0 && debugPos.z >= 0.0;
		float4 color = 0.0;
		int sampleIndex = 0; // Expose for manual analysis if necessary
		if (_texture1D && _textureArray)
		{
			color = debugTexture1DArray.Load(int3(debugPosInt.x, debugPosInt.y, mipLevel)); // The array elements are spread vertically
		}
		else if (_texture1D)
		{
			color = debugTexture1D.Load(int2(debugPosInt.x, mipLevel));
		}
		else if (_texture3D)
		{
			color = debugTexture3D.Load(int4(debugPosInt.x, debugPosInt.y, debugPosInt.z, mipLevel)); // The array elements are spread horizontally
		}
		// All "_texture2D" from here
		else if (_textureMS && _textureArray)
		{
			for (; sampleIndex < sampleCount; sampleIndex++)
			{
				color += debugTexture2DMSArray.Load(int3(debugPosInt.x, debugPosInt.y, debugPosInt.z), sampleIndex); // The array elements are spread horizontally
			}
		}
		else if (_textureMS)
		{
			for (; sampleIndex < sampleCount; sampleIndex++) // Take all MS samples, to get a better overview of the texture
			{
				color += debugTexture2DMS.Load(int2(debugPosInt.xy), sampleIndex); // Ideally we'd average them in linear space, but whatever
			}
		}
		else if (_textureArray)
		{
			color = debugTexture2DArray.Load(int4(debugPosInt.x, debugPosInt.y, debugPosInt.z, mipLevel)); // The array elements are spread horizontally
		}
		else
		{
			if (bilinear) // TODO: implement for other texture types
				color = debugTexture2D.SampleLevel(linearSampler, debugPos.xy / debugSize.xy, mipLevel);
			else
				color = debugTexture2D.Load(int3(debugPosInt.x, debugPosInt.y, mipLevel)); // Approximate to the closest texel (sharp!)
		}
		color /= sampleCount;

		// Samples on invalid coordinates should already return 0, but we force it anyway
		if (!validTexel)
		{
       		color.rgb = 0;
		}

		if (denormalize)
		{
			color.rgb = (color.rgb - 0.5) * 2.0; // Note: 0.5 wouldn't have been the exact center in UNORM textures, but it should do (otherwise we'd need to branch based on the format precision)
		}
		// Do it early so it also fixes nans
		if (doAbs)
		{
			color = abs(color);
		}
		if (doSaturate)
		{
			color = saturate(color);
		}
		if (redOnly)
		{
			color.rgb = color.r;
		}
		if (showAlpha)
		{
			color.rgb = color.a;
		}
		if (premultiplyAlpha)
		{
			color.rgb *= color.a;
		}
		if (UVToPixelSpace)
		{
			color.rgb *= debugSize;
		}
		if (invertColors) // Only works on in SDR range
		{
			color.rgb = 1.0 - color.rgb;
		}
		if (gammaToLinear) // Linearize (output expects linear) (use if image appeared too bright (gamma space viewed as linear))
		{
        	color.rgb = sRGB ? gamma_sRGB_to_linear(color.rgb, GCT_MIRROR) : (pow(abs(color.rgb), DefaultGamma) * Sign_Fast(color.rgb));
		}
		if (linearToGamma) // Gammify (usually not necessary) (use if image appeared too dark (linear space viewed as gamma))
		{
       		color.rgb = sRGB ? linear_to_sRGB_gamma(color.rgb, GCT_MIRROR) : (pow(abs(color.rgb), 1.f / DefaultGamma) * Sign_Fast(color.rgb));
		}
		if (tonemap && !showAlpha)
		{
    		const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
#if 1 // No hue shifts, better analysis
    		color.rgb = RestoreLuminance(color.rgb, Reinhard::ReinhardRange(GetLuminance(color.rgb), MidGray, -1.0, peakWhite / gamePaperWhite).x, true);
    		color.rgb = CorrectOutOfRangeColor(color.rgb, true, true, 0.5, peakWhite / gamePaperWhite);
#else
  			color.rgb = Reinhard::ReinhardRange(color.rgb, MidGray, -1.0, peakWhite / gamePaperWhite);
#endif
		}
		if (validTexel || !backgroundPassthrough)
		{
			outColor = color * gamePaperWhite; // Scale by user paper white brightness just to make it more visible
			drew = true;
		}
	}
	return drew;
}
#endif

float3 ComposeUI(float3 pos, float3 linearSceneColor, float gamePaperWhite, float UIPaperWhite)
{
	float4 UIColor = uiTexture.Load((int3)pos.xyz);
	float UIIntensity = saturate(UIColor.a);
	float UIPaperWhiteScale = UIPaperWhite / gamePaperWhite;

	if (POST_PROCESS_SPACE_TYPE != 1)
	{
		// The scene and UI arrive in the game's native gamma encoding. Normalize the scene to UI paper white in linear space,
		// restore its original encoding for the blend, then return to the encoded post-process path after restoring the scale.
		float3 UIBackgroundLinear = game_gamma_to_linear(linearSceneColor.rgb, GCT_MIRROR) / UIPaperWhiteScale;
		float3 UIBackgroundGamma = linear_to_game_gamma(UIBackgroundLinear, GCT_MIRROR);
		float3 composedColorGamma = UIBackgroundGamma * (1.0 - UIIntensity) + UIColor.rgb;
		float3 linearComposedColor = game_gamma_to_linear(composedColorGamma, GCT_MIRROR) * UIPaperWhiteScale;
		return linear_to_game_gamma(linearComposedColor, GCT_MIRROR);
	}
	else
	{
		// Preserve the game's original gamma-space UI blending. Normalize the linear scene to the UI paper-white range,
		// encode it into the same space as the separated UI, blend the premultiplied UI there, then decode and restore
		// the UI-relative scale. The final gamePaperWhite multiply consequently controls the scene while this scale
		// independently controls the UI.
		float3 UIBackgroundLinear = linearSceneColor.rgb / UIPaperWhiteScale;
		float3 UIBackgroundGamma;
		float3 linearComposedColor;

		// If gamma correction still occurs after composition, use sRGB as the intermediate storage/blending encoding;
		// the later correction makes it visually match gamma 2.2. If correction already happened, blend in gamma 2.2 directly.
		if ((!EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE <= 1) || GAMMA_CORRECTION_TYPE >= 2)
		{
			UIBackgroundGamma = linear_to_sRGB_gamma(UIBackgroundLinear, GCT_MIRROR);
			float3 composedColorGamma = UIBackgroundGamma * (1.0 - UIIntensity) + UIColor.rgb;
			linearComposedColor = gamma_sRGB_to_linear(composedColorGamma, GCT_MIRROR);
		}
		else
		{
			UIBackgroundGamma = linear_to_gamma(UIBackgroundLinear, GCT_MIRROR);
			float3 composedColorGamma = UIBackgroundGamma * (1.0 - UIIntensity) + UIColor.rgb;
			linearComposedColor = gamma_to_linear(composedColorGamma, GCT_MIRROR);
		}

		linearComposedColor *= UIPaperWhiteScale;
		return linearComposedColor;
	}
}

// Custom Luma shader to apply the display (or output) transfer function from a linear input (or apply custom gamma correction).
// Its result is linear (scRGB, 1 is 80 nits), as 1.0 wrote it to its scRGB swapchain: "EncodeOutput" then writes it for the real buffer.
float4 Compose(float4 pos)
{
	// Game scene paper white and Generic paper white for when we can't account for the UI paper white.
	// If "POST_PROCESS_SPACE_TYPE" or "EARLY_DISPLAY_ENCODING" are 1, this might have already been applied in.
	// This essentially means that the SDR range we receive at this point is 0-1 in the buffers, with 1 matching "sRGB_WhiteLevelNits" as opposued to "ITU_WhiteLevelNits".
    float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;

#if DEVELOPMENT
	float4 debugTextureColor = 0.0;
	if (HasStage(STAGE_DEVELOPMENT) && DrawDebugTexture(pos.xyz, debugTextureColor, gamePaperWhite, UIPaperWhite))
	{
		return debugTextureColor;
	}
#endif

	float sourceWidth;
	float sourceHeight;
	sourceTexture.GetDimensions(sourceWidth, sourceHeight);

	if (HasStage(STAGE_DEVELOPMENT) && TEST_2X_ZOOM)
	{
		const float scale = 2.0;
		pos.xy = pos.xy / scale + float2(sourceWidth, sourceHeight) / (scale * 2.0);
	}

	float2 uv = pos.xy / float2(sourceWidth, sourceHeight);

#if 0 // TEST: draw gradient
	return float4(uv.x, uv.x, uv.x, 1.0) * 2;
#endif

	float targetHeight1 = 1080.0 * DVS4;
	float targetHeight2 = targetHeight1 * DVS5 * 0.5;
	float mipLevel1 = log2(sourceHeight / targetHeight1);
	float mipLevel2 = log2(sourceHeight / targetHeight2);
	const bool useBilinearScale = HasStage(STAGE_SCALE) && (LumaComposition.SourceScale.x != 1.0 || LumaComposition.SourceScale.y != 1.0);
	float4 color = useBilinearScale ? sourceTexture.SampleLevel(linearSampler, uv, 0) : sourceTexture.Load((int3)pos.xyz);
	float4 mipColor1 = 0.0;
	float4 mipColor2 = 0.0;
#if 0
	mipColor1 = sourceTexture.SampleLevel(linearSampler, uv, mipLevel1);
	mipColor2 = sourceTexture.SampleLevel(linearSampler, uv, mipLevel2);
	int offset = 5;
	mipColor1 = sourceTexture.Load((int3)pos.xyz + int3( offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset,  offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3( offset,  offset, 0)) +
				color;
	mipColor1 /= 5.0;
	offset = 30;
	mipColor2 = sourceTexture.Load((int3)pos.xyz + int3( offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset,  offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3( offset,  offset, 0)) +
				color;
	mipColor2 /= 5.0;
#endif
	//return mipColor2; //TODOFT

	// This case means the game currently doesn't have Luma custom shaders built in (fallback in case of problems), or has manually unloaded them
	const bool modActive = LumaData.CustomData1 == 0;
	if (!modActive)
	{
		// Native HDR10 PQ game output (Luma disabled/unloaded while presenting through Mirror swapchain)
		if (LumaData.CustomData1 == 3)
		{
			if (HasStage(STAGE_DECODE))
			{
				color.rgb = PQ_to_Linear(color.rgb);
				color.rgb *= (HDR10_MaxWhiteNits / sRGB_WhiteLevelNits);
				color.rgb = BT2020_To_BT709(color.rgb);
			}
			return float4(color.rgb, color.a);
		}
		// SDR was already linear, assuming we are outputting on scRGB HDR buffers (usually implies "POST_PROCESS_SPACE_TYPE" is 1)
		const bool vanillaSwapchainWasLinear = LumaData.CustomData1 == 2;
		// "VANILLA_ENCODING_TYPE" is expected to be 0 for this branch
		if (vanillaSwapchainWasLinear && HasStage(STAGE_GAMMA))
		{
			// SDR (on SDR)
			if (LumaSettings.DisplayMode <= 0)
			{
				// Nothing to do, the game would have encoded with sRGB, the display will decode sRGB with gamma 2.2 as it would have in vanilla SDR.
			}
			// HDR
			else
			{
				float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb, GCT_MIRROR), GCT_MIRROR);
				float luminanceGammaCorrected = gamma_to_linear(linear_to_sRGB_gamma(GetLuminance(color.rgb), GCT_POSITIVE).x, GCT_POSITIVE).x;
				float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
				if (GAMMA_CORRECTION_TYPE == 1)
				{
					color.rgb = colorGammaCorrectedByChannel;
				}
				else if (GAMMA_CORRECTION_TYPE == 2)
				{
					color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
					float3 colorGammaCorrectedByChannelMip1 = gamma_to_linear(linear_to_sRGB_gamma(mipColor1.rgb, GCT_MIRROR), GCT_MIRROR);
					float3 colorGammaCorrectedByChannelMip2 = gamma_to_linear(linear_to_sRGB_gamma(mipColor2.rgb, GCT_MIRROR), GCT_MIRROR);
					mipColor1.rgb = RestoreLuminance(mipColor1.rgb, colorGammaCorrectedByChannelMip1);
					mipColor2.rgb = RestoreLuminance(mipColor2.rgb, colorGammaCorrectedByChannelMip2);
				}
				else if (GAMMA_CORRECTION_TYPE == 3)
				{
					color.rgb = colorGammaCorrectedByLuminance;
				}
				else if (GAMMA_CORRECTION_TYPE >= 4)
				{
					color.rgb = RestoreHueAndChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel, 0.0, 1.0);
				}
			}
		}
		// SDR was gamma space, but now we are outputting on scRGB HDR buffers
		else
		{
			if (!HasStage(STAGE_DECODE))
			{
			}
			// SDR (on SDR)
			else if (LumaSettings.DisplayMode <= 0)
			{
				// On an 8/10-bit SDR swapchain (OutputEncoding == 2) we encode with gamma 2.2, so decode with gamma 2.2 to pass through.
				// On scRGB (OutputEncoding == 0), Windows encodes with sRGB while the display decodes with gamma 2.2, so decode with sRGB.
				color.rgb = (LumaComposition.OutputEncoding == 2) ? gamma_to_linear(color.rgb, GCT_NONE) : gamma_sRGB_to_linear(color.rgb, GCT_NONE);
			}
			// HDR (we assume this is the default case for Luma users/devs, this isn't an officially supported case anyway) (we ignore "GAMMA_CORRECTION_RANGE_TYPE" and "VANILLA_ENCODING_TYPE" here, it doesn't matter)
			else
			{
				color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, HasStage(STAGE_GAMMA) ? GAMMA_CORRECTION_TYPE : VANILLA_ENCODING_TYPE);

				mipColor1.rgb = ColorGradingLUTTransferFunctionOut(mipColor1.rgb, GAMMA_CORRECTION_TYPE);
				mipColor2.rgb = ColorGradingLUTTransferFunctionOut(mipColor2.rgb, GAMMA_CORRECTION_TYPE);
			}
		}
		
#if 0 // AntiBloom AutoHDR
		// TODO: add an "unclipping" mode that tries to detect the channel value before it clipped to 1 by querying smaller mips until one isn't 1, and then tries to increase the value in the center of the clipped highlight blob. Also try "CorrectPerChannelTonemapDesaturation" to resaturate per channel tonemaped highlights.
		float shoulderPow = 2.75f; // Default value, can be changed in the settings
		float maxShoulderPow = lerp(shoulderPow, 1.f, LumaSettings.DevSetting01); // Default value, can be changed in the settings
		float mipRatio1 = GetLuminance(mipColor1.rgb) / GetLuminance(color.rgb);
		float mipRatio2 = GetLuminance(mipColor2.rgb) / GetLuminance(color.rgb);
		float peak = 500.f;
		float maxPeak = lerp(peak, 800.f, LumaSettings.DevSetting02);
    	if (LumaSettings.DevSetting03 <= 0.0)
		{
			shoulderPow = lerp(shoulderPow, maxShoulderPow, saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb)));
			peak = lerp(peak, maxPeak, saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb)));
		}
		if (LumaSettings.DevSetting03 > 0.75)
		{
			color.rgb = mipColor2.rgb;
		}
		else if (LumaSettings.DevSetting03 > 0.5)
		{
			color.rgb = mipColor1.rgb;
		}
		else if (LumaSettings.DevSetting03 > 0.25)
		{
		}
		else
		{
			color.rgb = PumboAutoHDR(color.rgb, peak, LumaSettings.GamePaperWhiteNits, shoulderPow); // This won't multiply the paper white in, it just uses it as a modifier for the AutoHDR logic
		}
		if (LumaSettings.DevSetting03 <= 0.0)
		{
			color.rgb = Saturation(color.rgb, (saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb))* 0.333 + 1.0));
		}
#endif

#if DEVELOPMENT // Optionally clamp SDR and SDR on HDR modes (dev only)
		if (LumaSettings.DisplayMode != 1)
			color.rgb = saturate(color.rgb);
#endif
		color.rgb *= gamePaperWhite;
		return float4(color.rgb, color.a);
	}

	float postLinearizationScale = 1.0;

	if (UI_DRAW_TYPE == 2)
	{
		if (HasStage(STAGE_UI))
		{
			postLinearizationScale *= UIPaperWhite;
			if (EARLY_DISPLAY_ENCODING == 0)
				postLinearizationScale /= gamePaperWhite;
		}
	}
	else if (UI_DRAW_TYPE == 3)
	{
		if (!HasStage(STAGE_UI))
		{
		}
		else if (LumaData.CustomData4 != 0.f)
		{
			color.rgb = ComposeUI(pos.xyz, color.rgb, gamePaperWhite, UIPaperWhite);
		}
		else
		{
			color.rgb = ComposeUI(pos.xyz, color.rgb, UIPaperWhite, UIPaperWhite);
			gamePaperWhite = UIPaperWhite;
		}
	}

	// SDR: In this case, paper white (game and UI) would have been 1 (neutral for SDR), so we can ignore it if we want to
	if (LumaSettings.DisplayMode <= 0)
	{
		color.rgb = saturate(color.rgb);

		// 8/10-bit SDR swapchains encode with gamma 2.2 in EncodeOutput; scRGB SDR relies on Windows sRGB encoding
		const uint sdrTransferFunction = (LumaComposition.OutputEncoding == 2) ? LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 : LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
		if (POST_PROCESS_SPACE_TYPE == 1)
		{
			if (HasStage(STAGE_GAMMA))
				ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, (EARLY_DISPLAY_ENCODING != 0 && GAMMA_CORRECTION_TYPE < 2) ? GAMMA_CORRECTION_TYPE : VANILLA_ENCODING_TYPE, sdrTransferFunction, true);
		}
		else
		{
			if (HasStage(STAGE_DECODE))
				color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, sdrTransferFunction, false);
		}
	}
	// HDR and SDR in HDR
	else if (LumaSettings.DisplayMode >= 1)
	{
		if (POST_PROCESS_SPACE_TYPE != 1) // Gamma->Linear space
		{
#if GAMMA_CORRECTION_RANGE_TYPE == 1
			if (HasStage(STAGE_DECODE))
				color.rgb = HasStage(STAGE_GAMMA) ? ColorGradingLUTTransferFunctionOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE) : ColorGradingLUTTransferFunctionOut(color.rgb, VANILLA_ENCODING_TYPE);
#else
			if (HasStage(STAGE_DECODE))
				color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, HasStage(STAGE_GAMMA) ? GAMMA_CORRECTION_TYPE : VANILLA_ENCODING_TYPE);
#endif
		}
		else // POST_PROCESS_SPACE_TYPE == 1 (Linear->Linear space)
		{
			if (EARLY_DISPLAY_ENCODING != 0)
				color.rgb /= gamePaperWhite;

			if (EARLY_DISPLAY_ENCODING == 0 && GAMMA_CORRECTION_TYPE <= 1)
			{
				if (HasStage(STAGE_GAMMA))
					ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
			}
			else if (GAMMA_CORRECTION_TYPE >= 2)
			{
				if (HasStage(STAGE_GAMMA))
				{
					float3 colorInExcess = color.rgb - saturate(color.rgb); // Only correct in the 0-1 range
					color.rgb = saturate(color.rgb);

					float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb));
					float luminanceGammaCorrected = gamma_to_linear1(linear_to_sRGB_gamma1(GetLuminance(color.rgb)));
					float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
					if (GAMMA_CORRECTION_TYPE == 2)
						color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
					else if (GAMMA_CORRECTION_TYPE == 3)
						color.rgb = colorGammaCorrectedByLuminance;
					else if (GAMMA_CORRECTION_TYPE >= 4)
						color.rgb = RestoreHueAndChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel, 0.0, 1.0);

					color.rgb += colorInExcess;
				}
			}
		}

		color.rgb *= postLinearizationScale;

		bool gamutMap = HasStage(STAGE_GAMUT);
		// Clip to SDR range in SDR-on-HDR mode (DisplayMode == 2)
		if (LumaSettings.DisplayMode >= 2)
		{
			color.rgb = saturate(color.rgb);
			gamutMap = false;
		}

		float aspectRatio = sourceWidth / (float)sourceHeight;
		bool blackBar = false;
		if (HasStage(STAGE_DEVELOPMENT) && LumaSettings.DisplayMode == 1 && ShouldForceSDR(uv, false, blackBar, aspectRatio))
		{
			if (blackBar)
			{
				color.rgb = 0.0;
			}
			else
			{
				if (TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL)
				{
					color.rgb = saturate(color.rgb);
				}
				else // Tonemap instead of raw clipping if the game didn't natively implement our split view mode in the tonemapper
				{
					const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
					color.rgb = Reinhard::ReinhardRange(color.rgb, MidGray, peakWhite, 1.0, true); // Acknowledge the previous peak tonemapping to better compress from the HDR display to the SDR range
				}
				color.rgb = gamma_to_linear(round(linear_to_gamma(color.rgb) * 255.0) / 255.0); // Quantize to 8 bit
			}
		}

		if (gamutMap && GAMUT_MAPPING_TYPE > 0)
		{
			// Applying gamma correction could both generate negative (invalid) luminances and colors beyond the human visible range,
			// so here we try and fix them up.
			// Depending on the HDR tonemapper, film grain and sharpening math we used, prior passes might have also generated invalid colors.
			FixColorGradingLUTNegativeLuminance(color.rgb);

			bool sdr = LumaSettings.DisplayMode != 1; // "GAMUT_MAPPING_TYPE == 1" is "auto"
			if (GAMUT_MAPPING_TYPE == 2)
				sdr = true;
			else if (GAMUT_MAPPING_TYPE >= 3)
				sdr = false;
			if (sdr)
				color.rgb = SimpleGamutClip(color.rgb, false);
			else
				color.rgb = BT2020_To_BT709(SimpleGamutClip(BT709_To_BT2020(color.rgb), true)); // For scRGB HDR we could go even wider than BT.2020 (e.g. AP0) but it should overall do fine.
		}

		color.rgb *= gamePaperWhite;
	}

#if 0 // Test
	color.rgb = float3(1, 0, 0);
#endif

// #if TEST_USER_PEAK_FXAA == 0
// 	color.rgb = min(PeakWhiteNits / sRGB_WhiteLevelNits, color.rgb);
// #endif

	return float4(color.rgb, color.a);
}

// The last stage, which can't be switched off: what the real buffer's format and color space take
float4 EncodeOutput(float4 color)
{
	if (LumaComposition.OutputEncoding == 1) // HDR10: PQ BT.2020, 10000 nits at 1
	{
		color.rgb = Linear_to_PQ(max(BT709_To_BT2020(color.rgb) * (sRGB_WhiteLevelNits / HDR10_MaxWhiteNits), 0.0), GCT_POSITIVE);
	}
	else if (LumaComposition.OutputEncoding == 2) // SDR: gamma 2.2 for 8/10-bit G22_NONE_P709 buffers
	{
		color.rgb = linear_to_gamma(saturate(color.rgb), GCT_NONE);
	}
	return color;
}

float4 main(float4 pos : SV_Position) : SV_Target0
{
	// The scale stage: the real buffer can be another size than the game's image (bilinear when SourceScale != 1)
	if (HasStage(STAGE_SCALE))
		pos.xy = (floor(pos.xy) + 0.5) * LumaComposition.SourceScale;
	return EncodeOutput(Compose(pos));
}
