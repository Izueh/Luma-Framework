#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

// Textures 1 and 4 always come together, expanding the pixel shader input signature.
#if _0806297C || _0FFE0AED || _1BCED85C || _211FB55F || _27E3BE86 || _288047AA || _2F104EE3 || _33B467FB || _435E4F7A || _44880094 || _49644172 || _49D7718D || _49F6EFCE || _4DA41D5E || _4E137FCB || _5109AB83 || _6A62D2F7 || _6EA8AD80 || _7579CEAD || _7758DF4C || _7AFD9825 || _80524805 || _8321F836 || _86F8F7C9 || _8CE1E3FA || _939824ED || _97EE9E05 || _A0B3FA64 || _A393BEBF || _A78AC640 || _A857DBC1 || _B3BE6B94 || _B756FB04 || _B8D26E59 || _CA2E65D6 || _CB9FA7F0 || _D259AA98 || _DB3F0961 || _DD1033D8 || _E0B922A1 || _E79BD563 || _E87A3D68 || _EA9C5691 || _F345BA5D
#define TEXTURE1 1
#define TEXTURE4 1
#define MORE_TEXCOORDS 1

#if _0806297C || _0FFE0AED || _211FB55F || _27E3BE86 || _288047AA || _33B467FB || _435E4F7A || _44880094 || _49644172 || _49D7718D || _49F6EFCE || _4DA41D5E || _4E137FCB || _5109AB83 || _6A62D2F7 || _7579CEAD || _7AFD9825 || _80524805 || _8321F836 || _86F8F7C9 || _8CE1E3FA || _939824ED || _A0B3FA64 || _A393BEBF || _A78AC640 || _A857DBC1 || _B756FB04 || _CA2E65D6 || _D259AA98 || _DB3F0961 || _E0B922A1 || _EA9C5691
#define BLEND 1
#endif

#if _0806297C || _0FFE0AED || _211FB55F || _44880094 || _49644172 || _49D7718D || _49F6EFCE || _6A62D2F7 || _7579CEAD || _7AFD9825 || _8321F836 || _939824ED || _A393BEBF || _A78AC640 || _D259AA98 || _EA9C5691
#define DOUBLE_BLUR 1
#endif
#endif

// Sometimes the MB cbuffer variables are defined even if it's not used.
// Texture 3 is always used for MB.
#if _0806297C || _211FB55F || _27E3BE86 || _288047AA || _44880094 || _49D7718D || _4DA41D5E || _4F61099C || _6A62D2F7 || _7579CEAD || _86F8F7C9 || _8CE1E3FA || _939824ED || _9DE64DC9 || _A0B3FA64 || _B8D26E59 || _CB9FA7F0 || _DB3F0961 || _DD1033D8 || _E0B922A1 || _E79BD563 || _E87A3D68 || _EA9C5691 || _F345BA5D
#define MOTION_BLUR_CBUFFER 1
#if _27E3BE86 || _288047AA || _4DA41D5E || _4F61099C || _86F8F7C9 || _8CE1E3FA || _9DE64DC9 || _A0B3FA64 || _B8D26E59 || _CB9FA7F0 || _DB3F0961 || _DD1033D8 || _E0B922A1 || _E79BD563 || _E87A3D68 || _F345BA5D
#define MOTION_BLUR 1
#define TEXTURE3 1
#endif
#endif

// Sometimes the Blur cbuffer variables are defined even if it's not used.
#if _0806297C || _288047AA || _2F104EE3 || _33B467FB || _44880094 || _49644172 || _49F6EFCE || _5109AB83 || _6EA8AD80 || _7579CEAD || _7758DF4C || _8321F836 || _86F8F7C9 || _939824ED || _A0B3FA64 || _A857DBC1 || _B3BE6B94 || _B756FB04 || _D259AA98 || _DD1033D8 || _E0B922A1 || _E79BD563 || _E87A3D68 || _F345BA5D
#define BLUR_CBUFFER 1
#if _288047AA || _2F104EE3 || _33B467FB || _5109AB83 || _6EA8AD80 || _7758DF4C || _86F8F7C9 || _A0B3FA64 || _A857DBC1 || _B3BE6B94 || _B756FB04 || _DD1033D8 || _E0B922A1 || _E79BD563 || _E87A3D68 || _F345BA5D
#define BLUR 1
#endif
#endif

// Sometimes the DoF cbuffer variables are defined even if it's not used.
#if _1BCED85C || _211FB55F || _288047AA || _33B467FB || _435E4F7A || _44880094 || _49F6EFCE || _4DA41D5E || _5109AB83 || _6A62D2F7 || _6EA8AD80 || _7758DF4C || _7AFD9825 || _80524805 || _939824ED || _97EE9E05 || _A78AC640 || _B8D26E59 || _CB9FA7F0 || _D259AA98 || _DB3F0961 || _E0B922A1 || _E79BD563 || _F345BA5D
#define DOF_CBUFFER 1
#if _1BCED85C || _288047AA || _33B467FB || _435E4F7A || _4DA41D5E || _5109AB83 || _6EA8AD80 || _7758DF4C || _80524805 || _97EE9E05 || _B8D26E59 || _CB9FA7F0 || _DB3F0961 || _E0B922A1 || _E79BD563 || _F345BA5D
#define DOF 1
#endif
#endif

#if _0806297C || _0FFE0AED || _1BCED85C || _211FB55F || _27E3BE86 || _288047AA || _435E4F7A || _44880094 || _49644172 || _4E137FCB || _4F61099C || _5109AB83 || _6EA8AD80 || _7C04055C || _A0B3FA64 || _A78AC640 || _B3BE6B94 || _B756FB04 || _B8D26E59 || _D259AA98 || _DB3F0961 || _E79BD563 || _E87A3D68 || _EA9C5691
#define EDGE_AA 1
#endif

// Defaults:
#ifndef TEXTURE1
#define TEXTURE1 0
#endif
// Always true
#ifndef TEXTURE2
#define TEXTURE2 1
#endif
#ifndef TEXTURE3
#define TEXTURE3 0
#endif
#ifndef TEXTURE4
#define TEXTURE4 0
#endif
#ifndef MORE_TEXCOORDS
#define MORE_TEXCOORDS 0
#endif
#ifndef BLEND
#define BLEND 0
#endif
#ifndef MOTION_BLUR
#define MOTION_BLUR 0
#endif
#ifndef MOTION_BLUR_CBUFFER
#define MOTION_BLUR_CBUFFER 0
#endif
#ifndef DOF
#define DOF 0
#endif
#ifndef DOF_CBUFFER
#define DOF_CBUFFER 0
#endif
#ifndef EDGE_AA
#define EDGE_AA 0
#endif
#ifndef BLUR
#define BLUR 0
#endif
#ifndef BLUR_CBUFFER
#define BLUR_CBUFFER 0
#endif
#ifndef DOUBLE_BLUR
#define DOUBLE_BLUR 0
#endif

// Luma settings:
#ifndef ALLOW_EDGE_AA
#define ALLOW_EDGE_AA 1
#endif
#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 2
#endif
#ifndef ENABLE_BLOOM
#define ENABLE_BLOOM 1
#endif
#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

// Exclusively specify the pack offset for the first used variable.
// The other ones will be determined automatically, always incrementing by a float4 (1 offset).
// Note: we set some of them to float4 instead of float1 to enforce padding.
cbuffer _Globals : register(b0)
{
  float4 padding01[64]; // Simulate the "packoffset(c64)" from their design (the first cbuffer var always had that offset)
#if EDGE_AA || BLUR_CBUFFER
  float2 d019_SrcTexture0SizeInv;
#endif
#if BLEND
  float4 pp00_BlendAmount; // float1
#endif
  float3 pp03_BaseColor;
  float3 pp04_SceneColor;
#if DOF_CBUFFER
  float4 pp05_FocusDistance;
  float4 pp06_DofNear;
  float4 pp07_DofFar;
  float4 pp08_MaxBlur;
#endif
  row_major float3x4 pp12_ColorMatrix;
#if DOF_CBUFFER
  float4 pp13_MaxBlurNear; // float1
#endif
  row_major float3x4 pp14_BloomColorMatrix;
#if EDGE_AA
  float4 pp17_EdgeTrashold; // float1
  float4 pp18_EdgePower; // float1
  float4 pp19_EdgeWidth; // float1
#endif
#if BLUR_CBUFFER
  float4 pp20_RBlurNoblurRadius; // float1
  float4 pp21_RBlurBlendAmount; // float1
#endif
#if EDGE_AA || DOF_CBUFFER
  float4 pp24_NearFarTFovRAspect;
#endif
#if DOUBLE_BLUR
  float4 pp38_DoubleBlurDistance; // float2
#endif
#if MOTION_BLUR_CBUFFER
  row_major float4x4 pp39_ViewProjMatLastFrame;
  row_major float4x4 pp40_ViewProjMatInv;
  float4 pp41_MotionBlurScale; // float1
  float3 pp42_MotionBlurVelocity;
#endif
#if EDGE_AA
  float4 pp44_SmoothEdgesAmount; // float1
#endif
}

Texture2D<float> s040_DepthTexture : register(t0);
SamplerState s040_DepthTexture_sampler_s : register(s0);
Texture2D<float4> s050_PostProcessSrcTexture : register(t8);
SamplerState s050_PostProcessSrcTexture_sampler_s : register(s8);
// Registers increase for each of the optional added textures, and any combination is valid.
// Indentation grows the same way as the register numbers.
#if TEXTURE1
  Texture2D<float4> s051_PostProcessSrcTexture1 : register(t9);
  SamplerState s051_PostProcessSrcTexture1_sampler_s : register(s9);
  #if TEXTURE2
    Texture2D<float4> s052_PostProcessSrcTexture2 : register(t10);
    SamplerState s052_PostProcessSrcTexture2_sampler_s : register(s10);
    #if TEXTURE3 || (MOTION_BLUR_CBUFFER && !MOTION_BLUR)
      Texture2D<float4> s053_PostProcessSrcTexture3 : register(t11);
      SamplerState s053_PostProcessSrcTexture3_sampler_s : register(s11);
      #if TEXTURE4
        Texture2D<float4> s054_PostProcessSrcTexture4 : register(t12);
        SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s12);
      #endif
    #elif TEXTURE4
      Texture2D<float4> s054_PostProcessSrcTexture4 : register(t11);
      SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s11);
    #endif
  #elif TEXTURE3 || (MOTION_BLUR_CBUFFER && !MOTION_BLUR)
    Texture2D<float4> s053_PostProcessSrcTexture3 : register(t10);
    SamplerState s053_PostProcessSrcTexture3_sampler_s : register(s10);
    #if TEXTURE4
      Texture2D<float4> s054_PostProcessSrcTexture4 : register(t11);
      SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s11);
    #endif
  #elif TEXTURE4
    Texture2D<float4> s054_PostProcessSrcTexture4 : register(t10);
    SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s10);
  #endif
#elif TEXTURE2
  Texture2D<float4> s052_PostProcessSrcTexture2 : register(t9);
  SamplerState s052_PostProcessSrcTexture2_sampler_s : register(s9);
  #if TEXTURE3 || (MOTION_BLUR_CBUFFER && !MOTION_BLUR)
    Texture2D<float4> s053_PostProcessSrcTexture3 : register(t10);
    SamplerState s053_PostProcessSrcTexture3_sampler_s : register(s10);
    #if TEXTURE4
      Texture2D<float4> s054_PostProcessSrcTexture4 : register(t11);
      SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s11);
    #endif
  #elif TEXTURE4
    Texture2D<float4> s054_PostProcessSrcTexture4 : register(t10);
    SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s10);
  #endif
#elif TEXTURE3 || (MOTION_BLUR_CBUFFER && !MOTION_BLUR)
  Texture2D<float4> s053_PostProcessSrcTexture3 : register(t9);
  SamplerState s053_PostProcessSrcTexture3_sampler_s : register(s9);
  #if TEXTURE4
    Texture2D<float4> s054_PostProcessSrcTexture4 : register(t10);
    SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s10);
  #endif
#elif TEXTURE4
  Texture2D<float4> s054_PostProcessSrcTexture4 : register(t9);
  SamplerState s054_PostProcessSrcTexture4_sampler_s : register(s9);
#endif

#define cmp

void main(
  float4 v0 : SV_Position,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
#if MORE_TEXCOORDS
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
#endif
  out float4 o0 : SV_Target0)
{
  bool forceVanilla = ShouldForceSDR(v1.xy);

  float4 r0,r1,r2,r3,r4,r5,r6;
  int4 r1i;
  float4 sceneColor = s050_PostProcessSrcTexture.Sample(s050_PostProcessSrcTexture_sampler_s, v1.xy).xyzw;
  float depth = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, v1.xy).x;

#if EDGE_AA && ALLOW_EDGE_AA // TODO: disable by default in Luma? FXAA is better, why is it here then? This is enabled anyway. It doesn't seem to do anything? I can't tell. Maybe it's for object outlines or something...
  const float4 icb[] = { { 0, 0, 0.196172, 0},
                              { -1.000000, -1.000000, 0.076555, 0},
                              { -1.000000, 1.000000, 0.076555, 0},
                              { 1.000000, -1.000000, 0.076555, 0},
                              { 1.000000, 1.000000, 0.076555, 0},
                              { 0, 1.000000, 0.124402, 0},
                              { 1.000000, 0, 0.124402, 0},
                              { 0, -1.000000, 0.124402, 0},
                              { -1.000000, 0, 0.124402, 0} };

  r1.x = cmp(0 < pp17_EdgeTrashold.x);
  if (r1.x != 0) {
    r1.x = depth;
    r2.xy = pp19_EdgeWidth.x * d019_SrcTexture0SizeInv.xy;
    r1.yz = -d019_SrcTexture0SizeInv.xy * pp19_EdgeWidth.x + v1.xy;
    r1.y = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, r1.yz).x;
    r3.xyzw = r2.xyxy * float4(-1,1,1,-1) + v1.xyxy;
    r1.z = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, r3.xy).x;
    r1.w = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, r3.zw).x;
    r3.xy = d019_SrcTexture0SizeInv.xy * pp19_EdgeWidth.x + v1.xy;
    r2.w = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, r3.xy).x;
    r2.z = pp24_NearFarTFovRAspect.w * r2.y;
    r2.xy = pp24_NearFarTFovRAspect.zz * r2.zx;
    r2.xz = r2.xy * r1.xx;
    r3.xz = -r2.xz;
    r3.y = r1.y + -r1.x;
    r4.x = r1.z + -r1.x;
    r5.xyzw = float4(-1,1,1,-1) * r2.zxzx;
    r6.x = r1.w + -r1.x;
    r2.y = r2.w + -r1.x;
    r4.yz = r5.xy;
    r1.xyz = r4.xyz * r3.xyz;
    r1.xyz = r4.zxy * r3.yzx + -r1.xyz;
    r1.w = dot(r1.xyz, r1.xyz);
    r1.w = rsqrt(r1.w);
    r1.xyz = r1.xyz * r1.w;
    r6.yz = r5.zw;
    r3.xyz = r6.xyz * r2.xyz;
    r2.xyz = r6.zxy * r2.yzx + -r3.xyz;
    r1.w = dot(r2.xyz, r2.xyz);
    r1.w = rsqrt(r1.w);
    r2.xyz = r2.xyz * r1.w;
    r3.xyzw = 0.196172252 * sceneColor;
    r4.xyzw = r3.xyzw;
    r1i.w = 1;
    while (true) {
      if (r1i.w >= 9) break;
      r5.xy = icb[r1i.w].xy * d019_SrcTexture0SizeInv.xy;
      r5.xy = r5.xy * pp44_SmoothEdgesAmount.x + v1.xy;
      r5.xyzw = s050_PostProcessSrcTexture.Sample(s050_PostProcessSrcTexture_sampler_s, r5.xy).xyzw;
      r4.xyzw = r5.xyzw * icb[r1i.w].z + r4.xyzw;
      r1i.w += 1;
    }
    r4.x = pp18_EdgePower.x + r4.x;
    r1.x = dot(r1.xyz, r2.xyz);
    r1.x = cmp(r1.x < pp17_EdgeTrashold.x);
    r1.x = r1.x ? 1.0 : 0;
    r2.xyzw = r4.xyzw - sceneColor;
    sceneColor = r1.x * r2.xyzw + sceneColor;
  }
#endif // EDGE_AA && ALLOW_EDGE_AA

#if MOTION_BLUR
  r1.x = s053_PostProcessSrcTexture3.Sample(s053_PostProcessSrcTexture3_sampler_s, v1.xy).z;
  r1.x = cmp(r1.x < 1);
  if (r1.x != 0) {
    r2.xy = v1.xy * float2(2,-2) + float2(-1,1);
    r2.zw = float2(1,1);
    r3.x = dot(pp40_ViewProjMatInv._m00_m01_m02_m03, r2.xyww);
    r3.y = dot(pp40_ViewProjMatInv._m10_m11_m12_m13, r2.xyww);
    r3.z = dot(pp40_ViewProjMatInv._m20_m21_m22_m23, r2.xyww);
    r1.y = dot(pp40_ViewProjMatInv._m30_m31_m32_m33, r2.xyzw);
    r1.yzw = r3.xyz / r1.y;
    r2.xyz = r1.yzw * depth + pp42_MotionBlurVelocity.xyz;
    r2.w = 1;
    r3.x = dot(pp39_ViewProjMatLastFrame._m00_m01_m02_m03, r2.xyzw);
    r3.y = dot(pp39_ViewProjMatLastFrame._m10_m11_m12_m13, r2.xyzw);
    r1.y = dot(pp39_ViewProjMatLastFrame._m30_m31_m32_m33, r2.xyzw);
    r1.yz = r3.xy / r1.yy;
    r2.xy = v1.xy * float2(2,-2) + float2(-1,1);
    r1.yz = -r2.xy + r1.yz;
    r1.yz = pp41_MotionBlurScale.x * r1.yz;
    r2.xy = float2(0.125,0.125) * r1.yz;
    r1.x = 1.001 - depth;
    r2.z = -r2.y;
    r1.xy = r2.xz * r1.xx;
    r1.z = dot(r1.xy, r1.xy);
    r1.z = sqrt(r1.z);
    r1.xy = r1.xy / r1.zz;
    r1.z = min(0.005, r1.z);
    r2.xyz = sceneColor.xyz;
    r3.xy = v1.xy;
    r1i.w = 1;
    const int dofSamples = 8;
    while (true) {
      if (r1i.w >= dofSamples) break;
      r3.xy = r1.xy * r1.z + r3.xy;
      r2.w = s053_PostProcessSrcTexture3.SampleLevel(s053_PostProcessSrcTexture3_sampler_s, r3.xy, 0).z;
      r4.xyz = s050_PostProcessSrcTexture.SampleLevel(s050_PostProcessSrcTexture_sampler_s, r3.xy, 0).xyz;
      r2.xyz += lerp(r4.xyz, sceneColor.xyz, r2.w);
      r1i.w += 1;
    }
    sceneColor.xyz = r2.xyz / float(dofSamples);
  }
#endif // MOTION_BLUR

#if MORE_TEXCOORDS
  float3 bloomColor = s052_PostProcessSrcTexture2.Sample(s052_PostProcessSrcTexture2_sampler_s, v3.xy).xyz;
  float3 color4 = s054_PostProcessSrcTexture4.Sample(s054_PostProcessSrcTexture4_sampler_s, v4.xy).xyz; // TODO: what are these?
  float3 color1 = s051_PostProcessSrcTexture1.Sample(s051_PostProcessSrcTexture1_sampler_s, v2.xy).xyz;
#else // Bloom is always there
  float3 bloomColor = s052_PostProcessSrcTexture2.Sample(s052_PostProcessSrcTexture2_sampler_s, v2.xy).xyz;
#endif // MORE_TEXCOORDS

  float3 composedColor = sceneColor.xyz;

#if DOF
  r1.w = v1.y * 2 + -1;
  r2.y = pp24_NearFarTFovRAspect.w * r1.w;
  r2.x = v1.x * 2 + -1;
  r2.xy = pp24_NearFarTFovRAspect.zz * r2.xy;
  r1.xyz = depth;
  r1.xy *= r2.xy;
  r1.x = dot(r1.xyz, r1.xyz);
  r1.x = sqrt(r1.x);
  r1.y = cmp(r1.x >= pp05_FocusDistance.x);
  r1.z = -pp05_FocusDistance.x + r1.x;
  r1.w = pp07_DofFar.x + -pp05_FocusDistance.x;
  r1.w = 1 / r1.w;
  r1.z = saturate(r1.z * r1.w);
  r1.w = pp08_MaxBlur.x + -1;
  r1.z = r1.z * r1.w + 1;
  r1.x = pp05_FocusDistance.x + -r1.x;
  r1.w = -pp06_DofNear.x + pp05_FocusDistance.x;
  r1.w = 1 / r1.w;
  r1.x = saturate(r1.x * r1.w);
  r1.w = pp13_MaxBlurNear.x + -1;
  r1.x = r1.x * r1.w + 1;
  {
    float blendFactor = r1.y ? r1.z : r1.x;
    float3 blendFactors = saturate(blendFactor * float3(1,0.5,0.25) - 1.0);
    float3 blendedColor = lerp(composedColor, color4, blendFactors.x);
    blendedColor = lerp(blendedColor, color1, blendFactors.y);
    composedColor = lerp(blendedColor, bloomColor, blendFactors.z);
  }
#endif // DOF

#if BLEND
  // This takes care of blending our buffers if there was no other blend factor from blur or DoF
  {
    float blendFactor = pp00_BlendAmount.x * 7 + 1;
    float3 blendFactors = saturate(blendFactor * float3(1,0.5,0.25) - 1.0);

#if DOUBLE_BLUR
    // Doubling effect (e.g. drunkness)
    // A is "left" and B is "right"
    r6.xy = v1.xy - (v1.xy * pp38_DoubleBlurDistance.xy);
    float3 sceneColor_A = s050_PostProcessSrcTexture.Sample(s050_PostProcessSrcTexture_sampler_s, r6.xy).xyz;
    float3 sceneColor_B = s050_PostProcessSrcTexture.Sample(s050_PostProcessSrcTexture_sampler_s, r6.xy + pp38_DoubleBlurDistance.xy).xyz;
    r0.xy = v3.xy - (v3.xy * pp38_DoubleBlurDistance.xy);
    float3 bloomColor_A = s052_PostProcessSrcTexture2.Sample(s052_PostProcessSrcTexture2_sampler_s, r0.xy).xyz;
    float3 bloomColor_B = s052_PostProcessSrcTexture2.Sample(s052_PostProcessSrcTexture2_sampler_s, r0.xy + pp38_DoubleBlurDistance.xy).xyz;
    r4.xy = v4.xy - (v4.xy * pp38_DoubleBlurDistance.xy);
    float3 color4_A = s054_PostProcessSrcTexture4.Sample(s054_PostProcessSrcTexture4_sampler_s, r4.xy).xyz;
    float3 color4_B = s054_PostProcessSrcTexture4.Sample(s054_PostProcessSrcTexture4_sampler_s, r4.xy + pp38_DoubleBlurDistance.xy).xyz;
    r2.xy = v2.xy - (v2.xy * pp38_DoubleBlurDistance.xy);
    float3 color1_A = s051_PostProcessSrcTexture1.Sample(s051_PostProcessSrcTexture1_sampler_s, r2.xy).xyz;
    float3 color1_B = s051_PostProcessSrcTexture1.Sample(s051_PostProcessSrcTexture1_sampler_s, r2.xy + pp38_DoubleBlurDistance.xy).xyz;
    
    float3 blendedColor = lerp(sceneColor_A, color4_A, blendFactors.x); // We ingore "composedColor" up to this point, it would not have been used, use "sceneColor_A" and "sceneColor_B" instead
    blendedColor = lerp(blendedColor, color1_A, blendFactors.y);
    composedColor = lerp(blendedColor, bloomColor_A, blendFactors.z);
    
    blendedColor = lerp(sceneColor_B, color4_B, blendFactors.x);
    blendedColor = lerp(blendedColor, color1_B, blendFactors.y);
    composedColor += lerp(blendedColor, bloomColor_B, blendFactors.z);

    composedColor *= 0.5; // Average
#else
    float3 blendedColor = lerp(composedColor, color4, blendFactors.x);
    blendedColor = lerp(blendedColor, color1, blendFactors.y);
    composedColor = lerp(blendedColor, bloomColor, blendFactors.z);
#endif // DOUBLE_BLUR
  }
#endif

#if BLUR
  {
    r1.xz = float2(-0.5,-0.5) + v1.xy;
    r1.y = r1.x * (d019_SrcTexture0SizeInv.y / d019_SrcTexture0SizeInv.x);
    r0.x = sqrt(dot(r1.yz, r1.yz));
    r0.x = r0.x * 2 - pp20_RBlurNoblurRadius.x;
    r0.x = saturate(pp21_RBlurBlendAmount.x * r0.x);
    float blendFactor = r0.x * 7 + 1;
    float3 blendFactors = saturate(blendFactor * float3(1,0.5,0.25) - 1.0);
    float3 blendedColor = lerp(composedColor, color4, blendFactors.x);
    blendedColor = lerp(blendedColor, color1, blendFactors.y);
    composedColor = lerp(blendedColor, bloomColor, blendFactors.z);
  }
#endif // BLUR
  
  float colorGradingIntensity = 1.0; // TODO: expose grading intensity and default it to 66%? We already have "IMPROVED_COLOR_GRADING_TYPE" and "ENABLE_COLOR_GRADING"...
  float raisedBlacksLowering = 0.0; // This is being used in places where we guess the game would raise shadow
  float crushedBlacksLowering = 0.0; // This is being used in places where we guess the game would crush shadow
#if IMPROVED_COLOR_GRADING_TYPE >= 3
  if (!forceVanilla)
  {
#if IMPROVED_COLOR_GRADING_TYPE == 3
    // TODO: find even better defaults... the situation is very tricky as if we unbalance the raise and crush, the levels get messed up, ambient lighting looks off (too contrasty), mood is gone etc. Maybe we could find the matching raise and crush setting strength by analyzing the current offsets...
    colorGradingIntensity = 0.85; // Overall the grading looked unnatural and too heavy, slightly reduce it.
    raisedBlacksLowering = 0.5; // Additive filters were too strong for modern displays.
    crushedBlacksLowering = 0.8; // Lowering this too much can cause crushed blacks, or well, anyway create too much constrast. This is basically used to simulate ambient lighting.
#else // Drastically reduce grading raise and crush on shadow, and reduce the overall intensity. This will have a similar visibily level to the non graded image, though shadow might look crushed.
    colorGradingIntensity = 0.75;
    raisedBlacksLowering = 1.0;
    crushedBlacksLowering = 1.0;
#endif
#if DEVELOPMENT && 0
    colorGradingIntensity = DVS1;
    raisedBlacksLowering = DVS2;
    crushedBlacksLowering = DVS3;
#endif
  }
#endif
#if !ENABLE_COLOR_GRADING
  colorGradingIntensity = 0.0;
#endif // !ENABLE_COLOR_GRADING

#if ENABLE_BLOOM
  {
    // Luma settings:
    float bloomIntensity = 1.0;
    bool fadeBloomHighlights = true; // Enabled by default given that when it's off, bloom is too discolored, bright and stepped (I tried a lot of math, but couldn't find a better way, it already looks good anyway!)
    float fadeBloomHighlightsIntensity = 1.0; // Only acknowledged if "fadeBloomHighlights" is true
    bool linearSpaceBloom = false; // Disabled as it looks way too faint (it's barely visible)

    float3 filteredBloomColor = bloomColor; // The filter is always run on the raw bloom color. It dims bloom a lot, without always coloring it!

#if IMPROVED_COLOR_GRADING_TYPE >= 1
    if (!forceVanilla && !fadeBloomHighlights) // This prevents bloom having steps from a random color to black, though it also makes bloom slightly stronger (due to it being present in shadow as well, and having a stronger highlights coloration)
    {
      filteredBloomColor.r = dot(pp14_BloomColorMatrix._m00_m01_m02, bloomColor);
      filteredBloomColor.g = dot(pp14_BloomColorMatrix._m10_m11_m12, bloomColor);
      filteredBloomColor.b = dot(pp14_BloomColorMatrix._m20_m21_m22, bloomColor);

      // Note: depending on "IMPROVED_COLOR_GRADING_TYPE", we could run this in BT.2020 like we do for "pp12_ColorMatrix" below, to slightly expand gamut, however it doesn't really matter for bloom!
      float3 matrixAdd = pp14_BloomColorMatrix._m03_m13_m23;
#if 0 // Doesn't work, the output it either still stepped (broken gradients), or too bright
      float offsetReductionIntensity = 0.5;
      filteredBloomColor = EmulateShadowOffset(filteredBloomColor, matrixAdd * offsetReductionIntensity, false, false) + (matrixAdd * (1.0 - offsetReductionIntensity));
#elif 1 // Do it by average so we avoid randomly coloring bloom due to clipping values below 0. This prevents it from having ugly colors and steps on gradients.
      float filteredBloomColorAverage = average(filteredBloomColor);
      float offsetReductionIntensity = 0.75;
      float filteredBloomColorOffsettedAverage = (EmulateShadowOffset(filteredBloomColorAverage, average(matrixAdd) * offsetReductionIntensity, false, false) + (average(matrixAdd) * (1.0 - offsetReductionIntensity))).x; // TODO: this assumes "pp14_BloomColorMatrix._m03_m13_m23" has the same value on rgb!!! It usually does.
      filteredBloomColor *= safeDivision(filteredBloomColorOffsettedAverage,  filteredBloomColorAverage, 1);
#elif 0 // Similar to above, but by linear luminance instead of average (overkill and possibly less "correct")
      filteredBloomColor = gamma_to_linear(filteredBloomColor, GCT_MIRROR);
      filteredBloomColor = RestoreLuminance(filteredBloomColor, gamma_to_linear1(linear_to_gamma1(GetLuminance(filteredBloomColor), GCT_MIRROR) + average(matrixAdd), GCT_MIRROR));
      filteredBloomColor = linear_to_gamma(filteredBloomColor, GCT_MIRROR);
#else
      filteredBloomColor += matrixAdd;
#endif
    }
    else
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
    {
      // Note: after this, bloom is almost all black given it subtracts a fixed rgb threshold. This means its color filter will exclusively apply to highlights, causing large steps (broken gradients) in the image if the filter is too strong (unless we only blend it in for shadow/midtones).
      // Note: the rgb multiplier is actually usually neutral, and so is the subtractive part. What tints bloom is actually clipping values below zero, given it's done per channel, so colors get very shifted.
      filteredBloomColor.r = dot(pp14_BloomColorMatrix._m00_m01_m02_m03, float4(bloomColor, 1.0));
      filteredBloomColor.g = dot(pp14_BloomColorMatrix._m10_m11_m12_m13, float4(bloomColor, 1.0));
      filteredBloomColor.b = dot(pp14_BloomColorMatrix._m20_m21_m22_m23, float4(bloomColor, 1.0));
    }

#if 0 // Test: debug filtered bloom
    o0.rgb = filteredBloomColor; return;
#endif
    
    // Apply the filter on a hypothetical color of white.
    // Useful to normalize the bloom in case it was ever needed
    float3 neutralBloomFilter = float3(dot(pp14_BloomColorMatrix._m20_m21_m22_m23, 1.0), dot(pp14_BloomColorMatrix._m10_m11_m12_m13, 1.0), dot(pp14_BloomColorMatrix._m20_m21_m22_m23, 1.0));

    float bloomColorGradingIntensity = colorGradingIntensity;
#if 0 // We have a new implementation of "fadeBloomHighlights==false" above
    if (!forceVanilla && !fadeBloomHighlights) // Disable bloom grading in highlights, otherwise they'd cause steps in the image
      bloomColorGradingIntensity *= saturate(max3(filteredBloomColor / neutralBloomFilter));
#endif

    float3 bloomColorHighlights = max(filteredBloomColor, 0.0);
    bool forceBloomColorHighlightsCalculation = fadeBloomHighlights; // Calculate it in linear space for better results
    // Remove any bloom filter color if requested
    if (bloomColorGradingIntensity != 1.0 || forceBloomColorHighlightsCalculation) // Avoid math issues with these functions in case they aren't meant to do anything
    {
      filteredBloomColor = gamma_to_linear(filteredBloomColor, GCT_MIRROR);
      bloomColorHighlights = RestoreLuminance(gamma_to_linear(bloomColor, GCT_MIRROR), max(filteredBloomColor, 0.0));
      float3 bloomColorHighlightsLocal = RestoreLuminance(gamma_to_linear(bloomColor, GCT_MIRROR), filteredBloomColor); // Let negative luminance through here
      filteredBloomColor = lerp(bloomColorHighlightsLocal, filteredBloomColor, bloomColorGradingIntensity); // Make it preserve the luminance but keep the old color ratio
      filteredBloomColor = linear_to_gamma(filteredBloomColor, GCT_POSITIVE);
      bloomColorHighlights = linear_to_gamma(bloomColorHighlights, GCT_POSITIVE);
    }
    // Clip out negatives, given there'd be a lot of them from the dimming
    else
    {
      filteredBloomColor = max(filteredBloomColor, 0.0);
    }

    if (!forceVanilla) // Luma: don't darken the scene when bloom is added, we are in HDR, we don't need to!!! We still need to clamp the negative values given they are way below 0 (due to subtractive generation). If we don't do this, bloomed highlights get clipped to their bloom value.
    {
      if (linearSpaceBloom) // Luma: add them in linear space to minimize hue distortions
      {
        composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
        filteredBloomColor = gamma_to_linear(filteredBloomColor, GCT_POSITIVE);
        bloomColor = gamma_to_linear(bloomColor, GCT_MIRROR);
      }

      if (!fadeBloomHighlights) // Note: this makes car headlights clip too much? Anyway it causes broken gradients because bloom highlights are highly colored, so they need to be faded out when too bright. The new branch below looks better anyway so this is fine!
      {
        filteredBloomColor *= bloomIntensity;
        composedColor += filteredBloomColor;
      }
      else // Luma: more vanilla conservative alternative, blend in bloom depending on the ratio between the scene and bloom color (this should allow a bit more HDR bloom, though it'd also risk causing more broken gradients in highlights). This can still have some "posterization", mostly visible in motion.
      {
#if 0
        float3 bloomBackgroundPassthrough = 1.0 - min(filteredBloomColor, 1.0);
#else // Disable if it causes posterization
        float3 bloomBackgroundPassthrough = 1.0 - min(filteredBloomColor / max(composedColor.xyz, 1.0), 1.0); // The filtered bloom is usually much lower than 1 even on highlights, so this doesn't darken by much
#endif
        bloomBackgroundPassthrough = lerp(bloomBackgroundPassthrough, 1.0, fadeBloomHighlightsIntensity);
        //bloomColorHighlights = lerp(bloomColorHighlights, bloomColor, sqr(saturate((bloomColor - 0.5) * 2.0))); // Creates more steps for some reason...
        filteredBloomColor = lerp(filteredBloomColor, bloomColorHighlights, fadeBloomHighlightsIntensity * saturate(bloomColor * 1.333)); // Restore the non filtered bloomed scene color on filtered bloom highlights (Luma values)
        filteredBloomColor *= bloomIntensity;
        composedColor = filteredBloomColor + (composedColor.xyz * bloomBackgroundPassthrough); // Higher bloom reduces the background color, not itself (original logic is to avoid clipping in SDR)
      }

      if (linearSpaceBloom)
      {
        composedColor = linear_to_gamma(composedColor, GCT_MIRROR);
      }
    }
    else
    {
      float3 bloomBackgroundPassthrough = 1.0 - min(filteredBloomColor, 1.0);
      composedColor = filteredBloomColor + (composedColor.xyz * bloomBackgroundPassthrough);
    }
  }
#endif // ENABLE_BLOOM

#if ENABLE_COLOR_GRADING
  const float3 preLevelsAndFilerColor = composedColor;

  float3 levelMul = pp04_SceneColor.xyz;
  float3 levelAdd = pp03_BaseColor.xyz;
#if IMPROVED_COLOR_GRADING_TYPE >= 1
  if (!forceVanilla) // Luma: fix raised blacks and expand gamut etc!!!
  {
#if IMPROVED_COLOR_GRADING_TYPE >= 2 // Note: we do this in SDR as well, as we later gamut map it back to sRGB
    // Re-create the multiplier for BT.2020.
    // We basically simulate what the multiplier would do to a neutral (white) color, and then convert it to BT.2020.
    // The difference is that if we now apply this one to a BT.2020 colors, if there's any multiplier channel below 1, gamut will expand.
    float3 levelMulBT2020 = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(levelMul, GCT_MIRROR)), GCT_MIRROR);
    levelMul = levelMulBT2020;
    // Re-create the addend for BT.2020.
    // We basically simulate what the addend would do to a neutral (black) color, and then convert it to BT.2020.
    // The difference is that if we now apply this one to a BT.2020 colors, if there's any subtraction, gamut will expand,
    // but also the "EmulateShadowOffset" function below will clamp to 0, but in BT.2020 it will have more room to expand the gamut.
    // Even if, levels seem to mostly be additive here anyway!
    float neutralAdd = 0.0; // Might make it 1 as well, but that sounds more wrong
    float3 levelAddBT2020 = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(levelAdd + neutralAdd, GCT_MIRROR)), GCT_MIRROR) - neutralAdd;
    levelAdd = levelAddBT2020;

    // Convert color to BT.2020 (keep in gamma space)
    composedColor = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(composedColor, GCT_MIRROR)), GCT_MIRROR);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2

    composedColor = MultiplyExtendedGamutColor(composedColor, levelMul);
    composedColor = EmulateShadowOffset(composedColor, levelAdd * raisedBlacksLowering, false) + (levelAdd * (1.0 - raisedBlacksLowering));

#if IMPROVED_COLOR_GRADING_TYPE >= 2
    composedColor = linear_to_gamma(BT2020_To_BT709(gamma_to_linear(composedColor, GCT_MIRROR)), GCT_MIRROR);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2
  }
  else
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  {
    composedColor *= levelMul;
    composedColor += levelAdd;
#if 0 // Luma: removed saturate
    composedColor = saturate(composedColor);
#endif
  }

#if 1 // Main color grading
  float3 filteredFinalColor = composedColor;
#if IMPROVED_COLOR_GRADING_TYPE >= 1
  if (!forceVanilla) // Luma: fix raised blacks from the last column of the matrix, which was purely additive and was crushing blacks a lot, after having raised them with the levels above!!!
  {
    // This one will already potentially expand the gamut, there's no way to convert it to BT.2020 as we can't project a single multiplier to it (it's a matrix, so every channel depend on every other channel)
    filteredFinalColor.r = dot(pp12_ColorMatrix._m00_m01_m02, composedColor);
    filteredFinalColor.g = dot(pp12_ColorMatrix._m10_m11_m12, composedColor);
    filteredFinalColor.b = dot(pp12_ColorMatrix._m20_m21_m22, composedColor);

    float3 matrixAdd = pp12_ColorMatrix._m03_m13_m23;
    
#if IMPROVED_COLOR_GRADING_TYPE >= 2 // Do it in BT.2020, see the levels code above for comments (this usually removes color so it will nicely expand shadow)
    float neutralAdd = 0.0;
    float3 matrixAddBT2020 = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(matrixAdd + neutralAdd, GCT_MIRROR)), GCT_MIRROR) - neutralAdd;
    matrixAdd = matrixAddBT2020;

    filteredFinalColor = linear_to_gamma(BT709_To_BT2020(gamma_to_linear(filteredFinalColor, GCT_MIRROR)), GCT_MIRROR);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2

    filteredFinalColor = EmulateShadowOffset(filteredFinalColor, matrixAdd * crushedBlacksLowering, false) + (matrixAdd * (1.0 - crushedBlacksLowering));
    
#if IMPROVED_COLOR_GRADING_TYPE >= 2
    filteredFinalColor = BT2020_To_BT709(gamma_to_linear(filteredFinalColor, GCT_MIRROR));
#if 0 // TODO: leave this out? negative channels might still be raised by later code, and thus recovered (however unlikely, they weren't in vanilla)
		FixColorGradingLUTNegativeLuminance(filteredFinalColor); // Make sure there's no negative luminance for the following passes, they wouldn't really help much
#endif
    filteredFinalColor = linear_to_gamma(filteredFinalColor, GCT_MIRROR);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2
  }
  else
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  {
    filteredFinalColor.r = dot(pp12_ColorMatrix._m00_m01_m02_m03, float4(composedColor, 1.0));
    filteredFinalColor.g = dot(pp12_ColorMatrix._m10_m11_m12_m13, float4(composedColor, 1.0));
    filteredFinalColor.b = dot(pp12_ColorMatrix._m20_m21_m22_m23, float4(composedColor, 1.0));
  }

  composedColor = filteredFinalColor;
#endif

  composedColor = lerp(preLevelsAndFilerColor, composedColor, colorGradingIntensity);
#endif // ENABLE_COLOR_GRADING

#if DEVELOPMENT && 0 // Test gamut expansion persisting through post processing
  composedColor = gamma_to_linear(composedColor, GCT_MIRROR);
  composedColor = Saturation(composedColor, DVS10 + 1);
  composedColor = linear_to_gamma(composedColor, GCT_MIRROR);
#endif // DEVELOPMENT

  o0.xyzw = float4(composedColor.rgb, sceneColor.a);
}