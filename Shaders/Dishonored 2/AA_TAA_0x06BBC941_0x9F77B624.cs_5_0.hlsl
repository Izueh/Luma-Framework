#include "../Includes/Common.hlsl"

struct postfx_luminance_autoexposure_t
{
   float EngineLuminanceFactor;   // Offset:    0
   float LuminanceFactor;         // Offset:    4
   float MinLuminanceLDR;         // Offset:    8
   float MaxLuminanceLDR;         // Offset:   12
   float MiddleGreyLuminanceLDR;  // Offset:   16
   float EV;                      // Offset:   20
   float Fstop;                   // Offset:   24
   uint PeakHistogramValue;       // Offset:   28
};

cbuffer PerInstanceCB : register(b2)
{
   // xy ?, zw size
   float4 cb_positiontoviewtexture : packoffset(c0);
   // xy size, zw inverse size
   float4 cb_taatexsize : packoffset(c1);
   // xy dithering randomization value, zw size
   float4 cb_taaditherandviewportsize : packoffset(c2);
   float4 cb_postfx_tonemapping_tonemappingparms : packoffset(c3);
   float4 cb_postfx_tonemapping_tonemappingcoeffsinverse1 : packoffset(c4);
   float4 cb_postfx_tonemapping_tonemappingcoeffsinverse0 : packoffset(c5);
   float4 cb_postfx_tonemapping_tonemappingcoeffs1 : packoffset(c6);
   float4 cb_postfx_tonemapping_tonemappingcoeffs0 : packoffset(c7);
   uint2 cb_postfx_luminance_exposureindex : packoffset(c8);
   float2 cb_prevresolutionscale : packoffset(c8.z);
   float cb_env_autoexp_adapt_max_luminance : packoffset(c9);
   float cb_view_white_level : packoffset(c9.y);
   float cb_taaamount : packoffset(c9.z);
   float cb_postfx_luminance_customevbias : packoffset(c9.w);
}
  
// Dishonored 2 used this name for this variable, which was renamed to a better name for DOTO
static const float cb_env_tonemapping_white_level = cb_env_autoexp_adapt_max_luminance;

cbuffer PerViewCB : register(b1)
{
   float4 cb_alwaystweak : packoffset(c0);
   float4 cb_viewrandom : packoffset(c1);
   float4x4 cb_viewprojectionmatrix : packoffset(c2);
   float4x4 cb_viewmatrix : packoffset(c6);
   // xy apparently zero and zw appearently one? seemengly unrelated from jitters
   float4 cb_subpixeloffset : packoffset(c10);
   float4x4 cb_projectionmatrix : packoffset(c11);
   float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
   float4x4 cb_previousviewmatrix : packoffset(c19);
   float4x4 cb_previousprojectionmatrix : packoffset(c23);
   float4 cb_mousecursorposition : packoffset(c27);
   float4 cb_mousebuttonsdown : packoffset(c28);
   // xy and the jitter offsets in uv space (y is flipped), zw might be the same in another space or the ones from the previous frame
   float4 cb_jittervectors : packoffset(c29);
   float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
   float4x4 cb_inverseviewmatrix : packoffset(c34);
   float4x4 cb_inverseprojectionmatrix : packoffset(c38);
   // xy are the rendering resolution
   float4 cb_globalviewinfos : packoffset(c42);
   float3 cb_wscamforwarddir : packoffset(c43);
   uint cb_alwaysone : packoffset(c43.w);
   float3 cb_wscamupdir : packoffset(c44);
   // This seems to be true at all times for TAA
   uint cb_usecompressedhdrbuffers : packoffset(c44.w);
   float3 cb_wscampos : packoffset(c45);
   float cb_time : packoffset(c45.w);
   float3 cb_wscamleftdir : packoffset(c46);
   float cb_systime : packoffset(c46.w);
   float2 cb_jitterrelativetopreviousframe : packoffset(c47);
   float2 cb_worldtime : packoffset(c47.z);
   float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
   float2 cb_resolutionscale : packoffset(c48.z);
   float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
   float cb_framenumber : packoffset(c49.z);
   uint cb_alwayszero : packoffset(c49.w);
}

#define DISPATCH_BLOCK 16

#define DISABLE_TAA 0
// This fixes highlights being clipped in a couple places, and possibly avoids HDR colors from being clipped, and blacks from being raised
#define DISABLE_CLAMP 1
// This is "optional" as the output looks similar with and without it, but it should help improve the quality of TAA,
// the only reason they tonemapped was to theoretically store in UNORM textures (which only take a 0-1 SDR range),
// but then they actually stored TAA in float textures, and undid the tonemapping when reading them back,
// so it would have just lowered the quality of it really (I don't think they did this to normalize the history by the exposure of the previous frames, as that's not part of TM).
// Somehow this causes raised blacks when on...
#define DISABLE_TONEMAP 0
#define ALLOW_HDR_DITHER 1
// This fixes the red/blue/green random colors that generate in bloom
#define DISABLE_DITHER 1

groupshared struct { float val[36]; } g1[DISPATCH_BLOCK + 2];
groupshared struct { float val[72]; } g0[DISPATCH_BLOCK + 2];

SamplerState smp_linearclamp_s : register(s0);
// The history of TAA (the accumulation of the previous frames). Either R11G11B10F or R16G16B16A16F (same format as rw_taahistory_write)
Texture2D<float3> ro_taahistory_read : register(t0);
// MVs on x and y, default initialized values on z and w
Texture2D<float4> ro_motionvectors : register(t1);
// Jittered color buffer (pre-TAA)
Texture2D<float4> ro_viewcolormap : register(t2);
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t3);
// The output history of our TAA. Either R11G11B10F or R16G16B16A16F
RWTexture2D<float3> rw_taahistory_write : register(u0);
// De-jittered color output. Either R11G11B10F or R16G16B16A16F
RWTexture2D<float3> rw_taaresult : register(u1);

float3 linearize(float3 value)
{
#if 0 // Disabled //TODO
   return value;
#else // Made safe
   return sqr(abs(value)) * sign(value);
   //return sqr(value);
#endif
}

float3 vanillaTonemap_Inverse(float3 inputColor, bool inverse = false)
{
#if 1 // OG inv TM
   inverse = true;
   // Some threshold to skip tonemapping, or treat highlights differently.
   // Unless this threshold is 0, the tonemapper output won't be contiguous,
   // unless both "cb_postfx_tonemapping_tonemappingcoeffs0" and "cb_postfx_tonemapping_tonemappingcoeffs1" were equal,
   // or were specifically calculated to match at the threshold point (which is probably what's happening).
   bool3 tonemapThreshold = inputColor < (inverse ? cb_postfx_tonemapping_tonemappingparms.y : cb_postfx_tonemapping_tonemappingparms.x);

   // This isn't the actual inverse tonemap formula, it's just called inverse anyway for some reason
   float4 tonemappingCoeffs0 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse0 : cb_postfx_tonemapping_tonemappingcoeffs0;
   float4 tonemappingCoeffs1 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse1 : cb_postfx_tonemapping_tonemappingcoeffs1;

   float3 outputColor;
   float4 tonemappingCoeffs;
   // This is an "advanced" version of Reinhard with levels and other kind of curves/scaling (it seemengly supports negative input values properly).
   // Unless the coefficients have very specific values, this will not compress to exactly 0-1, and could end up clipping.
   // The tonemap coefficients are probably something like this:
   // x: exposure/brightness scaling (dividend). Likely close or identical to "y". Neutral value at 1.
   // y: exposure/brightness scaling (divisor). Likely close or identical to "x". Neutral value at 1.
   // z: additive brightness levelling. This can be used to raise or crush (clip) blacks. Neutral value at 0. This should generally be lower than "w".
   // w: neutral Reinhard value at 1. It's likely that it often revolves around that value.
   tonemappingCoeffs = tonemapThreshold.r ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.r = ((tonemappingCoeffs.x * inputColor.r) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.r) + tonemappingCoeffs.w);

   tonemappingCoeffs = tonemapThreshold.g ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.g = ((tonemappingCoeffs.x * inputColor.g) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.g) + tonemappingCoeffs.w);

   tonemappingCoeffs = tonemapThreshold.b ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.b = ((tonemappingCoeffs.x * inputColor.b) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.b) + tonemappingCoeffs.w);

   return outputColor;
#else
   float4 tonemappingCoeffs0 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse0 : cb_postfx_tonemapping_tonemappingcoeffs0;
   float4 tonemappingCoeffs1 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse1 : cb_postfx_tonemapping_tonemappingcoeffs1;

   float3 outputColor0 = (tonemappingCoeffs0.z - (tonemappingCoeffs0.w * inputColor.rgb)) / ((tonemappingCoeffs0.y * inputColor.rgb) - tonemappingCoeffs0.x);
   float3 outputColor1 = (tonemappingCoeffs1.z - (tonemappingCoeffs1.w * inputColor.rgb)) / ((tonemappingCoeffs1.y * inputColor.rgb) - tonemappingCoeffs1.x);

   //TODO: add threshold? Or actually, pick based on the distance from validity...
   bool3 valid0 = outputColor0 < (inverse ? cb_postfx_tonemapping_tonemappingparms.y : cb_postfx_tonemapping_tonemappingparms.x);
   bool3 valid1 = outputColor1 >= (inverse ? cb_postfx_tonemapping_tonemappingparms.y : cb_postfx_tonemapping_tonemappingparms.x);

   float3 outputColor = 0.0;
   if (valid0.r && valid1.r)
      outputColor.r = max(outputColor0.r, outputColor1.r);
   else if (valid0.r)
      outputColor.r = outputColor0.r;
   else if (valid1.r)
      outputColor.r = outputColor1.r;
   if (valid0.g && valid1.g)
      outputColor.g = max(outputColor0.g, outputColor1.g);
   else if (valid0.g)
      outputColor.g = outputColor0.g;
   else if (valid1.g)
      outputColor.g = outputColor1.g;
   if (valid0.b && valid1.b)
      outputColor.b = max(outputColor0.b, outputColor1.b);
   else if (valid0.b)
      outputColor.b = outputColor0.b;
   else if (valid1.b)
      outputColor.b = outputColor1.b;
   return outputColor;
#endif
}

// This doesn't seem to make much sense given that TAA was running before tonemapping and storing its history on a linear (R11G11B10F) texture.
// My guess is that they first wrote TAA on a R8G8B8A8 UNORM texture, and hence applied gamma and tonemap to it, and then converted to storing it in linear space and forgot about it.
// The "inverse" parameter is to use the approximate inverse tonemapper TAA came with, but our implementation is more accurate.
float3 vanillaTonemap(float3 inputColor, bool inverse = false)
{
#if 0 // TAA doesn't need tonemapping to work properly, in fact, it's probably worse (and more expensive) to run it (actually this breaks the output)
   return inputColor;
#endif
#if DISABLE_TONEMAP
   if (inverse)
   {
    return inputColor * (vanillaTonemap_Inverse(MidGray) / MidGray);
   }
   else
   {
    return inputColor * (MidGray / vanillaTonemap_Inverse(MidGray));
   }
#endif
#if _9F77B624 && !DISABLE_CLAMP // LUMA: removed unnecessary clamp that clips colors (this was only done in DOTO)
   if (inverse)
   {
      inputColor = min(inputColor, 1.0);
   }
#endif
   // Some threshold to skip tonemapping, or treat highlights differently.
   // Unless this threshold is 0, the tonemapper output won't be contiguous,
   // unless both "cb_postfx_tonemapping_tonemappingcoeffs0" and "cb_postfx_tonemapping_tonemappingcoeffs1" were equal,
   // or were specifically calculated to match at the threshold point (which is probably what's happening).
   bool3 tonemapThreshold = inputColor < (inverse ? cb_postfx_tonemapping_tonemappingparms.y : cb_postfx_tonemapping_tonemappingparms.x);

   // This isn't the actual inverse tonemap formula, it's just called inverse anyway for some reason
   float4 tonemappingCoeffs0 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse0 : cb_postfx_tonemapping_tonemappingcoeffs0;
   float4 tonemappingCoeffs1 = inverse ? cb_postfx_tonemapping_tonemappingcoeffsinverse1 : cb_postfx_tonemapping_tonemappingcoeffs1;

   float3 outputColor;
   float4 tonemappingCoeffs;
   // This is an "advanced" version of Reinhard with levels and other kind of curves/scaling (it seemengly supports negative input values properly).
   // Unless the coefficients have very specific values, this will not compress to exactly 0-1, and could end up clipping.
   // The tonemap coefficients are probably something like this:
   // x: exposure/brightness scaling (dividend). Likely close or identical to "y". Neutral value at 1.
   // y: exposure/brightness scaling (divisor). Likely close or identical to "x". Neutral value at 1.
   // z: additive brightness levelling. This can be used to raise or crush (clip) blacks. Neutral value at 0. This should generally be lower than "w".
   // w: neutral Reinhard value at 1. It's likely that it often revolves around that value.
   tonemappingCoeffs = tonemapThreshold.r ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.r = ((tonemappingCoeffs.x * inputColor.r) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.r) + tonemappingCoeffs.w);

   tonemappingCoeffs = tonemapThreshold.g ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.g = ((tonemappingCoeffs.x * inputColor.g) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.g) + tonemappingCoeffs.w);

   tonemappingCoeffs = tonemapThreshold.b ? tonemappingCoeffs0.xyzw : tonemappingCoeffs1.xyzw;
   outputColor.b = ((tonemappingCoeffs.x * inputColor.b) + tonemappingCoeffs.z) / ((tonemappingCoeffs.y * inputColor.b) + tonemappingCoeffs.w);

   return outputColor;
}

// Runs before tonemapping
[numthreads(DISPATCH_BLOCK, DISPATCH_BLOCK, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vGroupID : SV_GroupID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
#if DISABLE_TAA
   const uint2 vPixelPosUInt = vGroupID.xy * uint2(DISPATCH_BLOCK, DISPATCH_BLOCK) + vThreadIDInGroup.xy; // Equal to "vThreadID.xy"
   GroupMemoryBarrierWithGroupSync();
   //rw_taaresult[vThreadID.xy] = ro_viewcolormap[vThreadID.xy].rgb;
   rw_taaresult[vThreadID.xy] = ro_viewcolormap.SampleLevel(smp_linearclamp_s, ((vThreadID.xy + 0.5) * cb_taatexsize.zw) - (cb_jittervectors.xy * float2(1, -1)), 0).rgb; // "fast" dejitter
#else

   // Read the source color in chunks, in some weird misaligned way

   // Pixel coordinates over a single number (from the top left, going right and then down)
   float2 groupTopLeftCoords;
   groupTopLeftCoords.x = (vThreadIDInGroup.y * DISPATCH_BLOCK) + vThreadIDInGroup.x;
   // Center around the texel uv
   groupTopLeftCoords.x += 0.5;

   // Round coordinates (or something like that, split them by block)
   groupTopLeftCoords.x *= 1.f / (DISPATCH_BLOCK + 2);
   groupTopLeftCoords.y = floor(groupTopLeftCoords.x);
   groupTopLeftCoords.x = frac(groupTopLeftCoords.x);
   groupTopLeftCoords.x *= DISPATCH_BLOCK + 2;
   groupTopLeftCoords.x = floor(groupTopLeftCoords.x);

   int2 groupTopLeftCoordsInt = (int2)groupTopLeftCoords.xy;
   int2 groupTopLeftPixelCoords = -(int2)vThreadIDInGroup.xy + (int2)vThreadID.xy - int2(1, 1);
   float exposure = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor; //TODO: rename. This isn't exposure?
   float biased_exposure = exposure * exp2(-cb_postfx_luminance_customevbias);
   bool someBool = groupTopLeftCoordsInt.y < (DISPATCH_BLOCK - 2); //TODO
   const float exposure_view_white_level = cb_view_white_level * exposure;
   // Even horizontal lines (or uneven)
   if (someBool)
   {
      int2 pixelCoords = groupTopLeftPixelCoords.xy + groupTopLeftCoordsInt.xy;  // Not the same as "vThreadID.xy"
      float3 sourceColor = ro_viewcolormap.Load(int3(pixelCoords, 0)).xyz;
      sourceColor *= cb_usecompressedhdrbuffers ? exposure_view_white_level : 1.f;
#if !DISABLE_CLAMP // Disable vanilla clamping (without disabling this, HDR is clamped)
      sourceColor = max(0.0, sourceColor); // Clamp colors below 0
      sourceColor = min(cb_env_autoexp_adapt_max_luminance, sourceColor); // Clip colors beyond 1 (or whatever the white level was) (hopefully this isn't used to do fades to black)
#endif
      // It's unclear why they'd apply exposure twice (at least in case "cb_usecompressedhdrbuffers" was true)
      sourceColor *= biased_exposure;

      float3 tonemappedColor = vanillaTonemap(sourceColor);

      float2 motionVectors = ro_motionvectors.Load(int3(pixelCoords, 0)).xy;
      float motionVectorsSquaredLength = dot(motionVectors.xy, motionVectors.xy); // Sum of x and y squares (needs sqrt to find the length)
      uint2 someCoords = (uint2)groupTopLeftCoordsInt.xx << int2(4, 3);
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4] = tonemappedColor.r;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 1] = tonemappedColor.g;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 2] = tonemappedColor.b;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 3] = motionVectorsSquaredLength;
      g1[groupTopLeftCoordsInt.y].val[someCoords.y / 4] = motionVectors.x;
      g1[groupTopLeftCoordsInt.y].val[someCoords.y / 4 + 1] = motionVectors.y;
   }
   groupTopLeftCoordsInt.y += DISPATCH_BLOCK - 2;
   someBool = groupTopLeftCoordsInt.y < (DISPATCH_BLOCK + 2);
   // Uneven horizontal lines (or even)
   if (someBool)
   {
      int2 pixelCoords = (int2)groupTopLeftPixelCoords.xy + (int2)groupTopLeftCoordsInt.xy; // Not the same as "vThreadID.xy"
      float3 sourceColor = ro_viewcolormap.Load(int3(pixelCoords, 0)).xyz;
      sourceColor *= cb_usecompressedhdrbuffers ? exposure_view_white_level : 1.f;
#if !DISABLE_CLAMP // Disable vanilla clamping (without disabling this, HDR is clamped)
      sourceColor = max(0.0, sourceColor); // Clamp colors below 0
      sourceColor = min(cb_env_autoexp_adapt_max_luminance, sourceColor); // Clip colors beyond 1 (or whatever the white level was) (hopefully this isn't used to do fades to black)
#endif
      // It's unclear why they'd apply exposure twice (at least in case "cb_usecompressedhdrbuffers" was true)
      sourceColor *= biased_exposure;

      float3 tonemappedColor = vanillaTonemap(sourceColor);

      float2 motionVectors = ro_motionvectors.Load(int3(pixelCoords, 0)).xy;
      float motionVectorsSquaredLength = dot(motionVectors.xy, motionVectors.xy); // Sum of x and y squares (needs sqrt to find the length)
      uint2 someCoords = (uint2)groupTopLeftCoordsInt.xx << int2(4, 3);
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4] = tonemappedColor.r;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 1] = tonemappedColor.g;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 2] = tonemappedColor.b;
      g0[groupTopLeftCoordsInt.y].val[someCoords.x / 4 + 3] = motionVectorsSquaredLength;
      g1[groupTopLeftCoordsInt.y].val[someCoords.y / 4] = motionVectors.x;
      g1[groupTopLeftCoordsInt.y].val[someCoords.y / 4 + 1] = motionVectors.y;
   }

   GroupMemoryBarrierWithGroupSync();

   // Actually do TAA

   float2 pixelCoords = (int2)vThreadID.xy + 0.5; // Center around the texel uv

   // Dithering (or film grain)
#if 1 // Double the size of film grain
   float2 ditherPixelCoords = (((uint2)vThreadID.xy / 2) * 2) + 0.5;
#else
   float2 ditherPixelCoords = pixelCoords;
#endif
   int ditherI = asint(dot(ditherPixelCoords.xy + cb_taaditherandviewportsize.xy, float2(2531011.75, 214013.15625)));
   ditherI = ((ditherI * ditherI * 0x00003d73) + 0x000c0ae5) * ditherI;
   ditherI = (uint)ditherI >> 9;
   ditherI = ditherI + 0x3f800000;
   float dither = (2.0 - asfloat(ditherI)) * 0.6 - 0.3;

   uint3 indexes;
   indexes.x = (uint)vThreadIDInGroup.x << 4;
   indexes.y = indexes.x + DISPATCH_BLOCK;
   indexes.z = indexes.y + DISPATCH_BLOCK;

   // Read back the colors in chunks
   float3 tonemappedColorSumGamma = 0;
   float3 tonemappedColorSumLinear = 0;
   float4 r3, r4;
   r3.xyz = float3(-1, 0, 0); // x is "minMotionVectorsSquaredLength"
   r4.xy = (int2)vThreadIDInGroup.xy;

   //TODO: make for loop? by 9 or 3
   float3 tonemappedColor = float3(g0[r4.y].val[indexes.x / 4], g0[r4.y].val[indexes.x / 4 + 1], g0[r4.y].val[indexes.x / 4 + 2]);
   float motionVectorsSquaredLength = g0[r4.y].val[indexes.x / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor); // Linearize (not needed with LUMA? Actually was it needed at all in DH2 given render here was still linear?)
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.xy) : r3.xyz;
   r4.xyzw = (int4)vThreadIDInGroup.xxyy + int4(1, 2, 0, 0);

   tonemappedColor = float3(g0[r4.w].val[indexes.y / 4], g0[r4.w].val[indexes.y / 4 + 1], g0[r4.w].val[indexes.y / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.w].val[indexes.y / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.xw) : r3.xyz;

   tonemappedColor = float3(g0[r4.z].val[indexes.z / 4], g0[r4.z].val[indexes.z / 4 + 1], g0[r4.z].val[indexes.z / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.z].val[indexes.z / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.yz) : r3.xyz;
   r4.xyzw = (int4)vThreadIDInGroup.xxyy + int4(0, 1, 1, 1);

   tonemappedColor = float3(g0[r4.w].val[indexes.x / 4], g0[r4.w].val[indexes.x / 4 + 1], g0[r4.w].val[indexes.x / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.w].val[indexes.x / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.xw) : r3.xyz;

   tonemappedColor = float3(g0[r4.z].val[indexes.y / 4], g0[r4.z].val[indexes.y / 4 + 1], g0[r4.z].val[indexes.y / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.z].val[indexes.y / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.yz) : r3.xyz;
   r4.xyzw = (int4)vThreadIDInGroup.xxyy + int4(2, 0, 2, 1);

   float3 tonemappedCenterColor = tonemappedColor;

   tonemappedColor = float3(g0[r4.w].val[indexes.z / 4], g0[r4.w].val[indexes.z / 4 + 1], g0[r4.w].val[indexes.z / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.w].val[indexes.z / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.xw) : r3.xyz;

   tonemappedColor = float3(g0[r4.z].val[indexes.x / 4], g0[r4.z].val[indexes.x / 4 + 1], g0[r4.z].val[indexes.x / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.z].val[indexes.x / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.yz) : r3.xyz;
   r4.xyzw = (int4)vThreadIDInGroup.xyxy + int4(1, 2, 2, 2);

   tonemappedColor = float3(g0[r4.y].val[indexes.y / 4], g0[r4.y].val[indexes.y / 4 + 1], g0[r4.y].val[indexes.y / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.y].val[indexes.y / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.xy) : r3.xyz;

   tonemappedColor = float3(g0[r4.w].val[indexes.z / 4], g0[r4.w].val[indexes.z / 4 + 1], g0[r4.w].val[indexes.z / 4 + 2]);
   motionVectorsSquaredLength = g0[r4.w].val[indexes.z / 4 + 3];
   tonemappedColorSumGamma += tonemappedColor;
   tonemappedColorSumLinear += linearize(tonemappedColor);
   r3.xyz = (r3.x < motionVectorsSquaredLength) ? float3(motionVectorsSquaredLength, r4.zw) : r3.xyz;

   const uint samples_num = 9;
   float4 r0, r1, r2, r6, r7, r8;
   r2.w = (uint)r3.y << 3; //TODO: cast int?
   float2 motionVectors = float2(g1[r3.z].val[r2.w / 4], g1[r3.z].val[r2.w / 4 + 1]);
   float2 jitteredPixelCoords = -motionVectors.xy * cb_taaditherandviewportsize.zw + pixelCoords.xy;
   float2 prevRelativeResScale = cb_prevresolutionscale.xy / cb_resolutionscale.xy;
   r3.zw = (jitteredPixelCoords * prevRelativeResScale) - 0.5;
   r3.zw = floor(r3.zw);
   r6.xyzw = float4(0.5, 0.5, -0.5, -0.5) + r3.zwzw;
   r0.xy = jitteredPixelCoords * prevRelativeResScale - r6.xy;
   r3.xy = sqr(r0.yx);
   r7.xy = r3.xy * r0.yx;
   r7.zw = r3.yx * r0.xy + r0.xy;
   r7.zw = -r7.zw * 0.5 + r3.yx;
   r8.xy = 2.5 * r3.yx;
   r7.xy = r7.yx * 1.5 - r8.xy;
   r7.xy = 1 + r7.xy;
   r0.xy = r3.xy * r0.yx - r3.xy;
   r3.xy = 0.5 * r0.xy;
   r8.xy = 1 - r7.wz;
   r8.xy = r8.xy - r7.yx;
   r0.xy = -r0.xy * 0.5 + r8.xy;
   r7.xy = r7.xy + r0.yx;
   r0.xy = r0.xy / r7.yx;
   r0.xy = r6.xy + r0.xy;
   r3.zw = 2.5 + r3.zw;
   r6.xw = cb_taatexsize.zw * r6.zw;
   r6.yz = cb_taatexsize.wz * r0.yx;
   r8.xy = cb_taatexsize.zw * r3.zw;
   const float2 maxUV = -cb_positiontoviewtexture.zw * 0.5 + cb_prevresolutionscale.xy;

   float historyTotalColorLuminance = 0;
   float historyColorMinLuminance = 1.00000003e+032;
   float historyColorMaxLuminance = -1.00000003e+032;
   float3 historyColorTMSum = 0;
   [unroll]
      for (uint i = 0; i < samples_num; i++)
      {
         float2 historyUV;
         float localExposure;
         switch (i)
         {
         default:
         case 0:
         historyUV = r6.xw;
         localExposure = r7.w * r7.z;
         break;
         case 1:
         historyUV = r6.zw;
         localExposure = r7.y * r7.z;
         break;
         case 2:
         historyUV = float2(r8.x, r6.w);
         localExposure = r3.x * r7.z;
         break;
         case 3:
         historyUV = r6.xy;
         localExposure = r7.x * r7.w;
         break;
         case 4:
         historyUV = r6.zy;
         localExposure = r7.y * r7.x;
         break;
         case 5:
         historyUV = float2(r8.x, r6.y);
         localExposure = r7.x * r3.x;
         break;
         case 6:
         historyUV = float2(r6.x, r8.y);
         localExposure = r3.y * r7.w;
         break;
         case 7:
         historyUV = float2(r6.z, r8.y);
         localExposure = r7.y * r3.y;
         break;
         case 8:
         historyUV = r8.xy;
         localExposure = r3.x * r3.y;
         break;
         }
         historyUV = clamp(cb_prevresolutionscale.xy * historyUV, 0.0, maxUV.xy);
         float3 historyColor = ro_taahistory_read.SampleLevel(smp_linearclamp_s, historyUV, 0).xyz;
         float3 historyColorTM = historyColor * (cb_usecompressedhdrbuffers ? exposure_view_white_level : 1.0);
         historyColorTMSum += historyColorTM * localExposure;
         float historyColorLuminance = GetLuminance(historyColorTM);
         historyTotalColorLuminance += historyColorLuminance;
         historyColorMinLuminance = min(historyColorMinLuminance, historyColorLuminance);
         historyColorMaxLuminance = max(historyColorMaxLuminance, historyColorLuminance);
      }

   // Lower film grain (up to zero) as we get closer to white
#if ALLOW_HDR_DITHER
   dither = (dither * clamp(abs(1.0 - GetLuminance(tonemappedCenterColor)), 0.1, 1.0)) + 1.0; // LUMA: allow a bit of film grain even on white and beyond!
#else
   dither = (dither * saturate(1.0 - GetLuminance(tonemappedCenterColor))) + 1.0; // Original version made HDR "compatible"
#endif
#if DISABLE_DITHER
    dither = 1.0;
#endif

   // Scale the sums of samples we summed up to normalize it
   float3 tonemappedColorAverageGamma = tonemappedColorSumGamma / samples_num;
   float3 tonemappedColorAverageLinear = tonemappedColorSumLinear / samples_num;
   float3 linearMinusGamma = tonemappedColorAverageLinear - linearize(tonemappedColorAverageGamma);
#if !DISABLE_CLAMP // Disable clamping and gammification
   linearMinusGamma = max(0.0, linearMinusGamma);
   linearMinusGamma = sqrt(linearMinusGamma);
#else
   linearMinusGamma = sqrt(abs(linearMinusGamma)) * sign(linearMinusGamma); // Made safe for negative values
#endif

   float3 someTMColor1 = min(tonemappedCenterColor * dither, tonemappedColorAverageGamma - linearMinusGamma);
   float3 someTMColor2 = max(tonemappedCenterColor * dither, tonemappedColorAverageGamma + linearMinusGamma);
   const float minLuminance = 0.1f;
   float colorLuminanceDiff = max(minLuminance, historyColorMaxLuminance - historyColorMinLuminance);
   float colorSomeLuminance = max(minLuminance, GetLuminance(tonemappedColorAverageGamma));
   float taaHistoryAmount = (max(minLuminance, historyTotalColorLuminance / samples_num) * max(minLuminance, GetLuminance(someTMColor2) - GetLuminance(someTMColor1))) / (colorLuminanceDiff * colorSomeLuminance);
   taaHistoryAmount = saturate(-1.f + taaHistoryAmount);
   taaHistoryAmount = (taaHistoryAmount * 2.f - 3.f) * sqr(taaHistoryAmount) + 1.f;
   float inverseTaaHistoryAmount = cb_taaamount * taaHistoryAmount + (1.f / 100.f);
   float3 semiFinalTAAColor = lerp(clamp(historyColorTMSum, someTMColor1, someTMColor2), tonemappedCenterColor * dither, inverseTaaHistoryAmount);
   float3 finalTAAColor = semiFinalTAAColor / (cb_usecompressedhdrbuffers ? exposure_view_white_level : 1.f);
   rw_taahistory_write[vThreadID.xy] = finalTAAColor;
   rw_taaresult[vThreadID.xy] = (vanillaTonemap(semiFinalTAAColor, true) / biased_exposure) / (cb_usecompressedhdrbuffers ? exposure_view_white_level : 1.f);
   //rw_taaresult[vThreadID.xy] = tonemappedCenterColor;
   //rw_taaresult[vThreadID.xy] = tonemappedColorAverageGamma * 7;
   //rw_taaresult[vThreadID.xy] = tonemappedColorSumLinear * 3;
#endif
}