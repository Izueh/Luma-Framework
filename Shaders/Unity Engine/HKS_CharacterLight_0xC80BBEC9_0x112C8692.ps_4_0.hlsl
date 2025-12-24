#include "../Includes/Common.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

#ifndef ENABLE_CHARACTER_LIGHT
#define ENABLE_CHARACTER_LIGHT 1
#endif

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  float minStep = 0.01;
#if ENABLE_LUMA // Luma: fix hero light having a visible step at the edge
  minStep = 0.0;
#endif
  r1.xyzw = r0.wxyz * v1.wxyz + float4(-minStep, -0.5,-0.5,-0.5);
  r0.xyzw = v1.wxyz * r0.wxyz;
  if (r1.x < 0.0) discard;
  r2.xy = v3.xy / v3.w;
  r2.xy += 1.0;
  r2.x = 0.5 * r2.x;
  r2.z = -r2.y * 0.5 + 1.0;
  float2 sceneUV = r2.xz;
  r2.xyzw = t1.Sample(s1_s, sceneUV).xyzw;
  r1.xyz = r1.yzw * 2.0 + r2.xyz;
  r2.xyz = r0.yzw * 2.0 + r2.xyz;
  r2.xyz -= 1.0;
  o0.w = r0.x;
#if 1 // Luma: character light intensity (1 is vanilla)
  float smoothnessParam = asfloat(LumaData.CustomData2);
  float intensityParam = LumaData.CustomData3;
  // TODO: there's a second character light that happens just before bloom at the end, PS hash: 0x7418DC7D, however it's probably a generic shader so it'd need a hook to add tweak params there

  if (o0.w > 0.0)
    o0.w = pow(o0.w / v1.w, smoothnessParam >= 1.0 ? remap(smoothnessParam, 1.0, 2.0, 1.0, 2.0) : remap(smoothnessParam, 0.0, 1.0, 0.667, 1.0)) * v1.w; // Normalize it before scaling, so we avoid doing a pow around the (e.g.) 0-0.5 range, which changes the intensity of the peak as well
  
  o0.w *= LumaData.CustomData3;
#endif
  o0.xyz = (0.5 < r0.yzw) ? r1.xyz : r2.xyz;
  
#if ENABLE_LUMA // Luma: fix character light having heavy banding, we found 5 bits to be a good value, even if it ends up showing a bit of grain. It needs to be applied on alpha too for best results.
  //o0.w *= 2.5; // Quick banding test
  ApplyDithering(o0.xyz, sceneUV, true, 1.0, 5, LumaSettings.FrameIndex, true);
  if (o0.w != 0.0)
  {
    float3 outAlpha = o0.w;
    ApplyDithering(outAlpha, sceneUV, true, 1.0, 5, LumaSettings.FrameIndex, true);
    o0.w = outAlpha.x; // Clip unused channels
  }
#elif _112C8692 // New game version added dither (anti banding), which is only active when enabling dithering in the settings. Luma disables it by default given it's got its own dithering (maybe we should allow both to run given that this branch is optional, but it's on default!!!).
  r1.x = dot(v0.xy, float2(0.0671105608,0.00583714992));
  r1.x = frac(r1.x);
  r1.x = 52.9829178 * r1.x;
  r1.x = frac(r1.x);
  r1.x = r1.x * 0.00392156886 + -0.00196078443;
  o0.xyzw += r1.x;
#endif

#if !ENABLE_CHARACTER_LIGHT
  o0.w = 0.0;
#endif
}