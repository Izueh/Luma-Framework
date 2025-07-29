#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl" // Use this as it has some gamma correction helpers

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> uiTexture : register(t1); // Optional: Pre-multiplied UI
Texture2D<float4> debugTexture : register(t2);

// Custom Luma shader to apply the display (or output) transfer function from a linear input (or apply custom gamma correction)
float4 main(float4 pos : SV_Position0) : SV_Target0
{
	// Game scene paper white and Generic paper white for when we can't account for the UI paper white.
	// If "POST_PROCESS_SPACE_TYPE" or "EARLY_DISPLAY_ENCODING" are 1, this might have already been applied in.
	// This essentially means that the SDR range we receive at this point is 0-1 in the buffers, with 1 matching "sRGB_WhiteLevelNits" as opposued to "ITU_WhiteLevelNits".
    const float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;

#if DEVELOPMENT
	float debugWidth;
	float debugHeight;
	debugTexture.GetDimensions(debugWidth, debugHeight);
	// Skip if there's no texture. It might be undefined behaviour, but it seems to work on Nvidia
	if (debugWidth != 0 && debugHeight != 0)
    {
		float2 resolutionScale = 1.0;
		bool fullscreen = (LumaData.CustomData2 & (1 << 0)) != 0;
        bool renderResolutionScale = (LumaData.CustomData2 & (1 << 1)) != 0;
        bool showAlpha = (LumaData.CustomData2 & (1 << 2)) != 0;
        bool premultiplyAlpha = (LumaData.CustomData2 & (1 << 3)) != 0;
		bool invertColors = (LumaData.CustomData2 & (1 << 4)) != 0;
        bool gammaToLinear = (LumaData.CustomData2 & (1 << 5)) != 0;
        bool linearToGamma = (LumaData.CustomData2 & (1 << 6)) != 0;
        bool flipY = (LumaData.CustomData2 & (1 << 7)) != 0;
        bool doSaturate = (LumaData.CustomData2 & (1 << 8)) != 0;
		bool backgroundPassthrough = false;

		if (fullscreen) // Stretch to fullscreen
		{
			float targetWidth;
			float targetHeight;
			sourceTexture.GetDimensions(targetWidth, targetHeight);
			resolutionScale = float2(debugWidth / targetWidth, debugHeight / targetHeight);
		}
		if (renderResolutionScale) // Scale by rendering resolution
		{
			resolutionScale *= LumaData.RenderResolutionScale;
		}
		
		if (flipY)
		{
			pos.y = debugHeight - pos.y;
		}

		pos.xy = round((pos.xy - 0.5) * resolutionScale) + 0.5;
		bool validTexel = pos.x < debugWidth && pos.y < debugHeight;
		float4 color = debugTexture.Load((int3)pos.xyz); // We don't have a sampler here so we just approimate to the closest texel

		if (doSaturate)
		{
			color = saturate(color);
		}
		if (showAlpha)
		{
			color.rgb = color.a;
		}
		if (premultiplyAlpha)
		{
			color.rgb *= color.a;
		}
		if (invertColors) // Only works on in SDR range
		{
			color.rgb = 1.0 - color.rgb;
		}
		if (gammaToLinear) // Linearize (output expects linear)
		{
        	color.rgb = pow(abs(color.rgb), DefaultGamma) * sign(color.rgb);
		}
		if (linearToGamma) // Gammify (usually not necessary)
		{
       		color.rgb = pow(abs(color.rgb), 1.f / DefaultGamma) * sign(color.rgb);
		}
		if (validTexel || !backgroundPassthrough)
		{
			return color * gamePaperWhite; // Scale by user paper white brightness just to make it more visible
		}
    }
#endif

#if DEVELOPMENT && 0 // TEST: zoom into the image to analyze it
	float targetWidth;
	float targetHeight;
	sourceTexture.GetDimensions(targetWidth, targetHeight);
	const float scale = 2.0;
	pos.xy = pos.xy / scale + float2(targetWidth, targetHeight) / (scale * 2.0);
#endif

	float4 color = sourceTexture.Load((int3)pos.xyz);

	// This case means the game currently doesn't have Luma custom shaders built in (fallback in case of problems), or has manually unloaded them, so the value of some macro defines do not matter
	const bool modActive = LumaData.CustomData1 == 0;
	if (!modActive)
	{
		// SDR was already linear, assuming we are outputting on scRGB HDR buffers (usually implies "POST_PROCESS_SPACE_TYPE" is 1)
		const bool vanillaSwapchainWasLinear = LumaData.CustomData1 >= 2;
		// "VANILLA_ENCODING_TYPE" is expected to be 0 for this branch
		if (vanillaSwapchainWasLinear)
		{
			// SDR (on SDR)
			if (LumaSettings.DisplayMode <= 0)
			{
				// Nothing to do, the game would have encoded with sRGB, the display will decode sRGB with gamma 2.2 as it would have in vanilla SDR, handling gamma correction for us.
				// "GAMMA_CORRECTION_TYPE" is not implemented here as it's not a common case that we'd wanna handle, and we want to keep SDR looking like SDR always did.
			}
			// HDR
			else
			{
				//TODOFT: wrap in a func, given it's duplicate below too? One of the ColorGradingLUTTransferFunctionIn...
				float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb, GCT_MIRROR), GCT_MIRROR);
				float luminanceGammaCorrected = gamma_to_linear(linear_to_sRGB_gamma(GetLuminance(color.rgb), GCT_POSITIVE).x, GCT_POSITIVE).x;
				float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
#if GAMMA_CORRECTION_TYPE == 1
				color.rgb = colorGammaCorrectedByChannel;
#elif GAMMA_CORRECTION_TYPE == 2
  				color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
#elif GAMMA_CORRECTION_TYPE == 3 //TODOFT: probably doesn't look good? It'd treat green and blue massively different
  				color.rgb = colorGammaCorrectedByLuminance;
#elif GAMMA_CORRECTION_TYPE >= 4
  				color.rgb = RestoreChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel);
#endif // GAMMA_CORRECTION_TYPE == 1
			}
		}
		// SDR was gamma space, but now we are outputting on scRGB HDR buffers
		else
		{
			// SDR (on SDR)
			if (LumaSettings.DisplayMode <= 0)
			{
				// The SDR display will (usually) linearize with gamma 2.2, hence applying the usual gamma mismatch, so we don't correct gamma here
				color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_NONE);
			}
			// HDR (we assume this is the default case for Luma users/devs, this isn't an officially supported case anyway) (we ignore "GAMMA_CORRECTION_RANGE_TYPE" and "VANILLA_ENCODING_TYPE" here, it doesn't matter)
			else
			{
				color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, GAMMA_CORRECTION_TYPE);
			}
		}
#if DEVELOPMENT // Optionally clamp SDR and SDR on HDR modes (dev only)
		if (LumaSettings.DisplayMode != 1)
			color.rgb = saturate(color.rgb);
#endif
		color.rgb *= gamePaperWhite;
		return float4(color.rgb, color.a);
	}
	
//TODOFT: split this and other code behaviours into functions
#if UI_DRAW_TYPE == 2 // The scene color was scaled by "scene paper white / UI paper white" (in linear space) to make the UI blend in correctly in gamma space without modifying its shaders

	float paperWhitePow = 1.0;
#if POST_PROCESS_SPACE_TYPE != 1 // Multiplying a gamma space color by a gammified ratio has the same results as multiplying the linear color by the original ratio (I know it doesn't sound intuitive) (this applies over multiple multiplications/divisions too, not just one)
	paperWhitePow = 1.0 / DefaultGamma;
#endif // POST_PROCESS_SPACE_TYPE != 1
  	color.rgb *= pow(UIPaperWhite, paperWhitePow);
#if !EARLY_DISPLAY_ENCODING
  	color.rgb /= pow(gamePaperWhite, paperWhitePow);
#endif // !EARLY_DISPLAY_ENCODING

#elif UI_DRAW_TYPE == 3 // Compose UI on top of "scene" and tonemap the scene background //TODOFT6: finish one (then clean up all the defines that we don't need anymore)

#if 1
	float3 sceneColorGamma = linear_to_sRGB_gamma(color.rgb, GCT_MIRROR);
	float3 UIRelativeColor = color.rgb * (gamePaperWhite / UIPaperWhite);
    float3 sceneColorGammaTonemapped = linear_to_sRGB_gamma((UIRelativeColor / (UIRelativeColor + 1.f)) / (gamePaperWhite / UIPaperWhite), GCT_MIRROR); // Tonemap the UI background based on the UI intensity to avoid bright backgrounds (e.g. sun) burning through the UI
	float3 UIInverseInfluence = 1.0;
	float4 UIColor = uiTexture.Load((int3)pos.xyz);
    float UIIntensity = saturate(UIColor.a);
	sceneColorGamma *= pow(gamePaperWhite, 1.0 / DefaultGamma);
	sceneColorGammaTonemapped *= pow(gamePaperWhite, 1.0 / DefaultGamma);
	UIColor.rgb *= pow(UIPaperWhite, 1.0 / DefaultGamma);
	// Darken the scene background based on the UI intensity
	float3 composedColor = lerp(sceneColorGamma, sceneColorGammaTonemapped, UIIntensity) * (1.0 - UIIntensity);
    // Calculate how much the additive UI influenced the darkened scene color, so we can determine the intensity to blend the composed color with the scene paper white (it's better to calculate this in gamma space)
	UIInverseInfluence = safeDivision(composedColor, composedColor + UIColor.rgb, 1); //TODO: handle negative colors?
	// Add pre-multiplied UI
	composedColor += UIColor.rgb;
	
	float3 compositionPaperWhite = lerp(pow(UIPaperWhite, 1.0 / DefaultGamma), pow(gamePaperWhite, 1.0 / DefaultGamma), UIInverseInfluence);
	composedColor /= compositionPaperWhite;

  	color.rgb = gamma_to_linear(composedColor, GCT_MIRROR) * pow(compositionPaperWhite, DefaultGamma);
#else
	color.rgb /= UIPaperWhite;
	float3 sceneColorGamma = linear_to_sRGB_gamma(color.rgb, GCT_MIRROR);
    float3 sceneColorGammaTonemapped = linear_to_sRGB_gamma(color.rgb / (color.rgb + 1.f), GCT_MIRROR); // Tonemap the UI background based on the UI intensity to avoid bright backgrounds (e.g. sun) burning through the UI
	float3 UIInverseInfluence = 1.0;
	float4 UIColor = uiTexture.Load((int3)pos.xyz);
    float UIIntensity = saturate(UIColor.a);
	// Darken the scene background based on the UI intensity
	float3 composedColor = lerp(sceneColorGamma, sceneColorGammaTonemapped, UIIntensity) * (1.0 - UIIntensity);
    // Calculate how much the additive UI influenced the darkened scene color, so we can determine the intensity to blend the composed color with the scene paper white (it's better to calculate this in gamma space)
	UIInverseInfluence = safeDivision(composedColor, composedColor + UIColor.rgb, 1);
	// Add pre-multiplied UI
	composedColor += UIColor.rgb;
  	color.rgb = gamma_to_linear(composedColor, GCT_MIRROR);
	color.rgb *= UIPaperWhite;
	color.rgb *= lerp(1.0, gamePaperWhite, saturate(UIInverseInfluence));
#endif

#endif // UI_DRAW_TYPE != 0

	// SDR: In this case, paper white (game and UI) would have been 1 (neutral for SDR), so we can ignore it if we want to
	if (LumaSettings.DisplayMode <= 0)
	{
		color.rgb = saturate(color.rgb); // Optional, but saves performance on the gamma pows below (the vanilla SDR tonemapper might have retained some values beyond 1 so we want to clip them anyway, for a "reliable" SDR look)

#if POST_PROCESS_SPACE_TYPE == 1
		// Revert whatever gamma adjustment "GAMMA_CORRECTION_TYPE" would have made, and get the color is sRGB gamma encoding (which would have been meant for 2.2 displays)
		// This function does more stuff than we need (like handling colors beyond the 0-1 range, which we've clamped out above), but we use it anyway for simplicity
		ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, (EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE < 2) ? GAMMA_CORRECTION_TYPE : VANILLA_ENCODING_TYPE, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, true);
#else // POST_PROCESS_SPACE_TYPE != 1
		// In SDR, we ignore "GAMMA_CORRECTION_TYPE" as they are not that relevant
		// We are target the gamma 2.2 look here, which would likely match the average SDR screen, so
		// we linearize with sRGB because scRGB HDR buffers (Luma) in SDR are re-encoded with sRGB and then (likely) linearized by the display with 2.2, which would then apply the gamma correction.
		// For any user that wanted to play in sRGB, they'd need to have an sRGB monitor.
		// We could theoretically add a mode that fakes sRGB output on scRGB->2.2 but it wouldn't really be useful as the game was likely designed for 2.2 displays (unconsciously).
		color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, false);
#endif // POST_PROCESS_SPACE_TYPE == 1

#if 0 // For linux support (somehow scRGB is not interpreted as linear when in SDR) //TODOFT4: expose?
		color.rgb = linear_to_sRGB_gamma(color.rgb, GCT_NONE);
#endif

	}
	// HDR and SDR in HDR: in this case the UI paper white would have already been mutliplied in, relatively to the game paper white, so we only apply the game paper white.
	else if (LumaSettings.DisplayMode >= 1)
	{
#if POST_PROCESS_SPACE_TYPE != 1 // Gamma->Linear space

		// At this point, in this case, the color would have been gamma space (sRGB or 2.2, depending on the game), normalized around SDR range (80 nits paper white).
		// The gamma correction both acts as correction but also as "absolute" gamma curve selection (emulating either an sRGB or Gamma 2.2 display).

#if GAMMA_CORRECTION_RANGE_TYPE == 1 // Apply gamma correction only in the 0-1 range

		color.rgb = ColorGradingLUTTransferFunctionOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE);
		
#else // GAMMA_CORRECTION_RANGE_TYPE != 0 // Apply gamma correction around the whole range (alternative branch) (this doesn't acknowledge "VANILLA_ENCODING_TYPE", it doesn't need to)

		color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, GAMMA_CORRECTION_TYPE);

#endif // GAMMA_CORRECTION_RANGE_TYPE == 1

#else // POST_PROCESS_SPACE_TYPE == 1 // Linear->Linear space

#if EARLY_DISPLAY_ENCODING
		// At this point, for this case, we expect the paper white to already have been multiplied in the color (earlier in the linear post processing pipeline)
		color.rgb /= gamePaperWhite;
#endif

#if !EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE <= 1

		ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true); // We enforce "GAMMA_CORRECTION_RANGE_TYPE" 1 as the other case it too complicated and unnecessary to implement

// "GAMMA_CORRECTION_TYPE >= 2" is always delayed until the end and treated as sRGB gamma before (independently of "EARLY_DISPLAY_ENCODING").
// We originally applied this gamma correction directly during tonemapping/grading and other later passes,
// but given that the formula is slow to execute and isn't easily revertible
// (mirroring back and forth is lossy, at least in the current lightweight implementation),
// we moved it to a single application here (it might not look as good but it's certainly good enough).
// Any linear->gamma->linear encoding (e.g. "PostAAComposites") or linear->gamma->luminance encoding (e.g. Anti Aliasing)
// should fall back on gamma 2.2 instead of sRGB for this gamma correction type, but we haven't bothered implementing that (it's not worth it).
#elif GAMMA_CORRECTION_TYPE >= 2

   		float3 colorInExcess = color.rgb - saturate(color.rgb); // Only correct in the 0-1 range
		color.rgb = saturate(color.rgb);

		float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb));
		float luminanceGammaCorrected = gamma_to_linear1(linear_to_sRGB_gamma1(GetLuminance(color.rgb)));
		float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
#if GAMMA_CORRECTION_TYPE == 2
		color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
#elif GAMMA_CORRECTION_TYPE == 3
  		color.rgb = colorGammaCorrectedByLuminance;
#elif GAMMA_CORRECTION_TYPE >= 4
  		color.rgb = RestoreChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel);
#endif // GAMMA_CORRECTION_TYPE == 2

		color.rgb += colorInExcess;

#endif // !EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE <= 1

#endif // POST_PROCESS_SPACE_TYPE != 1

		bool gamutMap = true;
#if DEVELOPMENT // Optionally clip in SDR to properly emulate SDR (dev only)
		if (LumaSettings.DisplayMode >= 2)
		{
			color.rgb = saturate(color.rgb);
			gamutMap = false;
		}
#endif

		if (gamutMap)
		{
			// Applying gamma correction could both generate negative (invalid) luminances and colors beyond the human visible range,
			// so here we try and fix them up.
			// Depending on the HDR tonemapper, film grain and sharpening math we used, prior passes might have also generated invalid colors.

#if GAMUT_MAPPING_TYPE > 0
			float3 preColor = color.rgb;

			FixColorGradingLUTNegativeLuminance(color.rgb);

			bool sdr = LumaSettings.DisplayMode != 1; // "GAMUT_MAPPING_TYPE == 1" is "auto"
#if GAMUT_MAPPING_TYPE == 2
			sdr = true;
#else // GAMUT_MAPPING_TYPE >= 3
			sdr = false;
#endif // GAMUT_MAPPING_TYPE == 2
			if (sdr)
			{
				color.rgb = SimpleGamutClip(color.rgb, false);
			}
			else
			{
				color.rgb = BT2020_To_BT709(SimpleGamutClip(BT709_To_BT2020(color.rgb), true)); // For scRGB HDR we could go even wider than BT.2020 (e.g. AP0) but it should overall do fine.
			}
#if 0 // Display gamut mapped colors
			if (any(abs(color.rgb - preColor.rgb) > 0.00001))
			{
				color.rgb = 100;
			}
#endif
#endif // GAMUT_MAPPING_TYPE > 0
		}
		
		color.rgb *= gamePaperWhite;
	}

#if 0 // Test
	color.rgb = float3(1, 0, 0);
#endif

	return float4(color.rgb, color.a);
}