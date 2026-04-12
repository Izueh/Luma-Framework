// ---- Created with 3Dmigoto v1.4.1 on Sun Apr  5 13:36:52 2026
#include "Includes/Tonemap.hlsli"

#define HDR_BLOOM_RESOLVE 1

cbuffer PSParamBuffer1 : register(b4)
{
  float4 g_Param : packoffset(c0);
  float g_CharacterAttenuation : packoffset(c1);
}

SamplerState g_LinearClampSampler_s : register(s0);
Texture2D<float4> g_TextureHDR : register(t0);
Texture2D<float4> g_TextureAdaptLumminance : register(t1);
Texture2D<float4> g_TextureEffectMask : register(t2);
Texture2D<float4> g_PrevTexture : register(t3);
Texture2D<float4> g_GeometryBuffer00 : register(t20);
Texture2D<uint4> g_StencilBuffer : register(t25);


// 3Dmigoto declarations
#define cmp -

#if !HDR_BLOOM_RESOLVE
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Build a material/stencil classification mask from local GBuffer neighborhood samples.
  int2 pixelCoord = int2(v0.xy + v0.xy);
  int3 pixelCoord3 = int3(pixelCoord, 0);
  r1.x = g_GeometryBuffer00.Load(pixelCoord3).w;                        // ASM: ld (0,0,0)
  r1.x = 255 * r1.x;
  r1.x = round(r1.x);
  r1.x = (uint)r1.x;
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(0, -1)).w;          // ASM: ld_aoffimmi(0,-1,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(-1, 0)).w;          // ASM: ld_aoffimmi(-1,0,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(1, 0)).w;           // ASM: ld_aoffimmi(1,0,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(0, 1)).w;           // ASM: ld_aoffimmi(0,1,0)
  uint stencilValue = g_StencilBuffer.Load(pixelCoord3).y;
  uint gbufBottom = (uint)round(255.0 * r1.y);
  uint gbufOr = ((uint)r1.x) | gbufBottom;                               // ASM: or
  bool noNeighborhood = (gbufOr == 0u);                                  // ASM: ieq r0.w, r0.z, 0
  uint stencilLow4 = stencilValue & 15u;
  bool stencilBit7Set = ((stencilValue & 128u) != 0u);
  bool materialBit32Set = ((gbufOr & 32u) != 0u);
  uint classTerm = stencilBit7Set ? 9u : 0u;                             // ASM: movc r0.y, r0.y, 9, 0
  bool characterGate = (((classTerm + stencilLow4) == 12u) && noNeighborhood); // ASM: iadd + ieq + and

  // Preserve the original decompiler register semantics used later in mainSDR.
  r0.x = characterGate ? 1.0 : 0.0;
  r0.z = materialBit32Set ? 1.0 : 0.0;

  // Gather 4-neighborhood HDR taps in opponent space to construct temporal clipping bounds.
  // ASM confirms this first tap uses integer sample offset (0, -1).
  r1.xyz = g_TextureHDR.Sample(g_LinearClampSampler_s, v1.xy, int2(0, -1)).xyz;
  r0.y = g_TextureAdaptLumminance.Sample(g_LinearClampSampler_s, float2(0.5,0.5)).x;
  r0.w = g_TextureEffectMask.Sample(g_LinearClampSampler_s, v1.xy).w;
  r0.w = cmp(0.00100000005 >= r0.w);
  r2.xy = r0.ww ? g_Param.xy : g_Param.zw;
  r0.y = r2.y * r0.y;
  r1.xyz = r0.yyy * r1.xyz;
  r2.yzw = r1.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r2.yzw = r1.xyz * r2.yzw + float3(0.00400000019,0.00400000019,0.00400000019);
  r3.xyz = r1.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r1.xyz = r1.xyz * r3.xyz + float3(0.0599999987,0.0599999987,0.0599999987);
  r1.xyz = r2.yzw / r1.xyz;
  r1.xyz = float3(-0.0666666701,-0.0666666701,-0.0666666701) + r1.xyz;
  r1.xyz = r1.xyz * r2.xxx;
  r2.yzw = float3(1.37906432,1.37906432,1.37906432) * r1.xyz;
  r1.xyz = r1.xyz * float3(1.37906432,1.37906432,1.37906432) + float3(1,1,1);
  r1.xyz = r2.yzw / r1.xyz;
  r3.x = dot(float3(0.25,0.5,0.25), r1.xyz);
  r3.y = dot(float2(0.5,-0.5), r1.xz);
  r3.z = dot(float3(-0.25,0.5,-0.25), r1.xyz);
  // ASM confirms this second tap uses integer sample offset (-1, 0).
  r1.xyz = g_TextureHDR.Sample(g_LinearClampSampler_s, v1.xy, int2(-1, 0)).xyz;
  r1.xyz = r1.xyz * r0.yyy;
  r2.yzw = r1.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r2.yzw = r1.xyz * r2.yzw + float3(0.00400000019,0.00400000019,0.00400000019);
  r4.xyz = r1.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r1.xyz = r1.xyz * r4.xyz + float3(0.0599999987,0.0599999987,0.0599999987);
  r1.xyz = r2.yzw / r1.xyz;
  r1.xyz = float3(-0.0666666701,-0.0666666701,-0.0666666701) + r1.xyz;
  r1.xyz = r1.xyz * r2.xxx;
  r2.yzw = float3(1.37906432,1.37906432,1.37906432) * r1.xyz;
  r1.xyz = r1.xyz * float3(1.37906432,1.37906432,1.37906432) + float3(1,1,1);
  r1.xyz = r2.yzw / r1.xyz;
  r4.x = dot(float3(0.25,0.5,0.25), r1.xyz);
  r4.y = dot(float2(0.5,-0.5), r1.xz);
  r4.z = dot(float3(-0.25,0.5,-0.25), r1.xyz);
  r1.xyz = max(r4.xyz, r3.xyz);
  r2.yzw = min(r4.xyz, r3.xyz);

  // ASM confirms this third tap uses integer sample offset (+1, 0).
  r3.xyz = g_TextureHDR.Sample(g_LinearClampSampler_s, v1.xy, int2(1, 0)).xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r4.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r4.xyz = r3.xyz * r4.xyz + float3(0.00400000019,0.00400000019,0.00400000019);
  r5.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r3.xyz = r3.xyz * r5.xyz + float3(0.0599999987,0.0599999987,0.0599999987);
  r3.xyz = r4.xyz / r3.xyz;
  r3.xyz = float3(-0.0666666701,-0.0666666701,-0.0666666701) + r3.xyz;
  r3.xyz = r3.xyz * r2.xxx;
  r4.xyz = float3(1.37906432,1.37906432,1.37906432) * r3.xyz;
  r3.xyz = r3.xyz * float3(1.37906432,1.37906432,1.37906432) + float3(1,1,1);
  r3.xyz = r4.xyz / r3.xyz;
  r4.x = dot(float3(0.25,0.5,0.25), r3.xyz);
  r4.y = dot(float2(0.5,-0.5), r3.xz);
  r4.z = dot(float3(-0.25,0.5,-0.25), r3.xyz);

  // ASM confirms this fourth tap uses integer sample offset (0, +1).
  r3.xyz = g_TextureHDR.Sample(g_LinearClampSampler_s, v1.xy, int2(0, 1)).xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r5.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r5.xyz = r3.xyz * r5.xyz + float3(0.00400000019,0.00400000019,0.00400000019);
  r6.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r3.xyz = r3.xyz * r6.xyz + float3(0.0599999987,0.0599999987,0.0599999987);
  r3.xyz = r5.xyz / r3.xyz;
  r3.xyz = float3(-0.0666666701,-0.0666666701,-0.0666666701) + r3.xyz;
  r3.xyz = r3.xyz * r2.xxx;
  r5.xyz = float3(1.37906432,1.37906432,1.37906432) * r3.xyz;
  r3.xyz = r3.xyz * float3(1.37906432,1.37906432,1.37906432) + float3(1,1,1);
  r3.xyz = r5.xyz / r3.xyz;
  r5.x = dot(float3(0.25,0.5,0.25), r3.xyz);
  r5.y = dot(float2(0.5,-0.5), r3.xz);
  r5.z = dot(float3(-0.25,0.5,-0.25), r3.xyz);
  r3.xyz = max(r5.xyz, r4.xyz);
  r4.xyz = min(r5.xyz, r4.xyz);
  r2.yzw = min(r4.xyz, r2.yzw);
  r1.xyz = max(r3.xyz, r1.xyz);
  r3.xyzw = g_TextureHDR.Sample(g_LinearClampSampler_s, v1.xy).xyzw;
  r3.xyz = r3.xyz * r0.yyy;
  o0.w = r3.w;
  r4.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.0500000007,0.0500000007,0.0500000007);
  r4.xyz = r3.xyz * r4.xyz + float3(0.00400000019,0.00400000019,0.00400000019);
  r5.xyz = r3.xyz * float3(0.150000006,0.150000006,0.150000006) + float3(0.5,0.5,0.5);
  r3.xyz = r3.xyz * r5.xyz + float3(0.0599999987,0.0599999987,0.0599999987);
  r3.xyz = r4.xyz / r3.xyz;
  r3.xyz = float3(-0.0666666701,-0.0666666701,-0.0666666701) + r3.xyz;
  r3.xyz = r3.xyz * r2.xxx;
  r4.xyz = float3(1.37906432,1.37906432,1.37906432) * r3.xyz;
  r3.xyz = r3.xyz * float3(1.37906432,1.37906432,1.37906432) + float3(1,1,1);
  r3.xyz = r4.xyz / r3.xyz;
  r4.y = dot(float2(0.5,-0.5), r3.xz);
  r4.x = dot(float3(0.25,0.5,0.25), r3.xyz);
  r4.z = dot(float3(-0.25,0.5,-0.25), r3.xyz);

  // Read previous-frame bloom history and compress to bounded domain before temporal clipping.
  r3.xyz = g_PrevTexture.Sample(g_LinearClampSampler_s, v1.xy).xyz;
  r5.xyz = float3(1,1,1) + r3.xyz;
  r3.xyz = r3.xyz / r5.xyz;
  r5.x = dot(float3(0.25,0.5,0.25), r3.xyz);
  r5.y = dot(float2(0.5,-0.5), r3.xz);
  r5.z = dot(float3(-0.25,0.5,-0.25), r3.xyz);
  r3.xyz = cmp(r5.xyz == float3(0,0,0));
  r0.y = r3.y ? r3.x : 0;
  r0.y = r3.z ? r0.y : 0;
  r3.xyz = r0.yyy ? r4.xyz : r5.xyz;

  // Clip history against current-frame neighborhood min/max envelope to reduce temporal artifacts.
  r2.xyz = max(r3.xyz, r2.yzw);
  r1.xyz = min(r2.xyz, r1.xyz);
  r2.x = dot(float3(1,1,-1), r4.xyz);
  r2.z = dot(float3(1,-1,-1), r4.xyz);
  r2.y = dot(float2(1,1), r4.xz);
  r2.yzw = (r2.xyz);
  r0.y = saturate(1 + -r2.x);
  r0.w = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r0.w * r0.y;
  r0.y = min(g_CharacterAttenuation, r0.y);
  r3.xyz = r1.xyz * r0.yyy;
  r0.xyw = r0.xxx ? r3.xyz : r1.xyz;

  // Reconstruct output from transformed channels through saturate + rational expansion stage.
  r1.x = (dot(float3(1,1,-1), r0.xyw));
  r1.y = (dot(float2(1,1), r0.xw));
  r1.z = (dot(float3(1,-1,-1), r0.xyw));
  r0.xyw = float3(0.985000014,0.985000014,0.985000014) * r1.xyz;
  r1.xyz = -r1.xyz * float3(0.985000014,0.985000014,0.985000014) + float3(1,1,1);
  r0.xyw = r0.xyw / r1.xyz;
  r1.xyz = float3(0.985000014,0.985000014,0.985000014) * r2.yzw;
  r2.xyz = -r2.yzw * float3(0.985000014,0.985000014,0.985000014) + float3(1,1,1);
  r1.xyz = r1.xyz / r2.xyz;
  r0.xyw = -r1.xyz + r0.xyw;
  r0.xyw = float3(0.980000019,0.980000019,0.980000019) * r0.xyw;
  r0.xyz = r0.zzz ? float3(0,0,0) : r0.xyw;
  o0.xyz = r1.xyz + r0.xyz;
  return;
}
#endif
// ---------------------------------------------------------------------------
// Opponent-colour basis helpers used by mainHDR.
//
// Forward:  RGB -> YC1C2
//   Y  =  0.25R + 0.5G + 0.25B   (luminance proxy)
//   C1 =  0.5R  - 0.5B           (red-blue opponent)
//   C2 = -0.25R + 0.5G - 0.25B   (yellow-blue opponent)
//
// Inverse:
//   R = Y + C1 - C2
//   G = Y + C2
//   B = Y - C1 - C2
// ---------------------------------------------------------------------------
#if HDR_BLOOM_RESOLVE
float3 ToOpponent(float3 c)
{
    return float3(
        dot(float3( 0.25,  0.5,  0.25), c),
        dot(float2( 0.5,  -0.5),        c.xz),
        dot(float3(-0.25,  0.5, -0.25), c)
    );
}

float3 FromOpponent(float3 o)
{
    return float3(
        dot(float3(1.0,  1.0, -1.0), o),   // R = Y + C1 - C2
        o.x + o.z,                           // G = Y + C2
        dot(float3(1.0, -1.0, -1.0), o)    // B = Y - C1 - C2
    );
}

// ---------------------------------------------------------------------------
// HDR-native temporal bloom resolve.
//
// Structural differences from main():
//   * No Hable-family tone curve and no 1.37906 rational applied per tap.
//     Neighbourhood bounds and clipping operate in scene-linear space.
//   * No Reinhard encode (x / (1+x)) on g_PrevTexture.  History is read
//     directly as linear HDR.
//   * No saturate before output; no 0.985 rational expansion (kx/(1-kx)).
//     The 98/2 temporal blend and the character attenuation are preserved
//     unchanged, all in scene-linear space.
// ---------------------------------------------------------------------------
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0, r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Convert NDC jitter to UV and subtract it so bloom taps are evaluated in stable screen space.
  float2 jitterUv = LumaData.GameData.JitterOffset * float2(0.5, -0.5);
  float2 bloomUv = v1.xy - jitterUv;

  // ---- Material / stencil classification (identical to main()) ----
  int2 pixelCoord = int2(v0.xy + v0.xy);
  int3 pixelCoord3 = int3(pixelCoord, 0);
  r1.x = g_GeometryBuffer00.Load(pixelCoord3).w;                        // ASM: ld (0,0,0)
  r1.x = 255 * r1.x;
  r1.x = round(r1.x);
  r1.x = (uint)r1.x;
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(0, -1)).w;          // ASM: ld_aoffimmi(0,-1,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(-1, 0)).w;          // ASM: ld_aoffimmi(-1,0,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(1, 0)).w;           // ASM: ld_aoffimmi(1,0,0)
  r1.y = 255 * r1.y;
  r1.y = round(r1.y);
  r1.y = (uint)r1.y;
  r1.x = (uint)r1.y | (uint)r1.x;                                       // ASM: or
  r1.y = g_GeometryBuffer00.Load(pixelCoord3, int2(0, 1)).w;           // ASM: ld_aoffimmi(0,1,0)
  uint stencilValue = g_StencilBuffer.Load(pixelCoord3).y;
  uint gbufBottom = (uint)round(255.0 * r1.y);
  uint gbufOr = ((uint)r1.x) | gbufBottom;                               // ASM: or
  bool noNeighborhood = (gbufOr == 0u);                                  // ASM: ieq r0.w, r0.z, 0
  uint stencilLow4 = stencilValue & 15u;
  bool stencilBit7Set = ((stencilValue & 128u) != 0u);
  bool materialBit32 = ((gbufOr & 32u) != 0u);                           // true for surfaces that suppress history delta
  uint classTerm = stencilBit7Set ? 9u : 0u;                             // ASM: movc r0.y, r0.y, 9, 0
  bool characterGate = (((classTerm + stencilLow4) == 12u) && noNeighborhood); // true inside character regions

  // ---- Auto-exposure scale ----
  float  adaptedLum  = g_TextureAdaptLumminance.Sample(g_LinearClampSampler_s, float2(0.5, 0.5)).x;
  float  effectMaskW = g_TextureEffectMask.Sample(g_LinearClampSampler_s, bloomUv).w;
  float2 params      = (effectMaskW <= 0.001) ? g_Param.xy : g_Param.zw;
  float  scale       = params.y * adaptedLum;
  // const float tonePeak    = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
  const float tonePeak    = 1.f;
  const float toneClip    = 100.f;
  const float toneGrayIn  = 0.18f;
  const float toneGrayOut = 0.067f; // Closer to vanilla Hable mid-gray mapping at x=0.18

  // ---- 4-neighbourhood HDR samples, scene-linear (no tone curve) ----
  float3 tapTop    = g_TextureHDR.Sample(g_LinearClampSampler_s, bloomUv, int2( 0, -1)).xyz * scale;
  float3 tapLeft   = g_TextureHDR.Sample(g_LinearClampSampler_s, bloomUv, int2(-1,  0)).xyz * scale;
  float3 tapRight  = g_TextureHDR.Sample(g_LinearClampSampler_s, bloomUv, int2( 1,  0)).xyz * scale;
  float3 tapBottom = g_TextureHDR.Sample(g_LinearClampSampler_s, bloomUv, int2( 0,  1)).xyz * scale;

  // float topScale = ComputeNeutwoMaxScaleGray(tapTop, tonePeak, toneClip, toneGrayIn, toneGrayOut);
  // float leftScale = ComputeNeutwoMaxScaleGray(tapLeft, tonePeak, toneClip, toneGrayIn, toneGrayOut);
  // float rightScale = ComputeNeutwoMaxScaleGray(tapRight, tonePeak, toneClip, toneGrayIn, toneGrayOut);
  // float bottomScale = ComputeNeutwoMaxScaleGray(tapBottom, tonePeak, toneClip, toneGrayIn, toneGrayOut);

  tapTop    = Tonemap_Bloom(tapTop);
  tapLeft   = Tonemap_Bloom(tapLeft);
  tapRight  = Tonemap_Bloom(tapRight);
  tapBottom = Tonemap_Bloom(tapBottom);

  float3 oppTop    = ToOpponent(tapTop);
  float3 oppLeft   = ToOpponent(tapLeft);
  float3 oppRight  = ToOpponent(tapRight);
  float3 oppBottom = ToOpponent(tapBottom);

  // Neighbourhood clipping envelope, computed in opponent space.
  float3 oppMax = max(max(oppTop, oppLeft), max(oppRight, oppBottom));
  float3 oppMin = min(min(oppTop, oppLeft), min(oppRight, oppBottom));

  // ---- Centre tap ----
  float4 centerFull = g_TextureHDR.Sample(g_LinearClampSampler_s, bloomUv);
  o0.w = centerFull.w;
  float3 tapCenter = centerFull.xyz * scale;
  // float centerScale = ComputeNeutwoMaxScaleGray(tapCenter, tonePeak, toneClip, toneGrayIn, toneGrayOut);
  tapCenter = Tonemap_Bloom(tapCenter);

  // ---- Temporal history: read as linear HDR, no Reinhard encode ----
  float3 history    = g_PrevTexture.Sample(g_LinearClampSampler_s, v1.xy).xyz;
  float3 oppHistory = ToOpponent(history);
  float3 oppCenter  = ToOpponent(tapCenter);

  // Zero-history fallback: seed from current centre on cold start.
  float3 oppCurrent = all(oppHistory == 0.0) ? oppCenter : oppHistory;

  // ---- Clip history to neighbourhood envelope ----
  float3 oppClipped = clamp(oppCurrent, oppMin, oppMax);

  // ---- Character attenuation (same smoothstep formula as main()) ----
  // Brightness proxy is tapCenter.r in linear space.  For HDR values r >= 1,
  // saturate(1 - r) = 0, so bright characters receive full history pass-through.
  float attIn = saturate(1.0 - tapCenter.r);
  float att   = attIn * attIn * (attIn * -2.0 + 3.0);
  att = min(g_CharacterAttenuation, att);

  float3 oppFinal = characterGate ? (oppClipped * att) : oppClipped;

  // ---- Final blend: centre + 0.98 * history_delta, scene-linear ----
  // materialBit32 surfaces (e.g. sky) receive no history delta contribution.
  float3 histRGB = FromOpponent(oppFinal);
  float3 delta   = (histRGB - tapCenter) * 0.98;

  o0.xyz = tapCenter + (materialBit32 ? float3(0.0, 0.0, 0.0) : delta);
}
#endif