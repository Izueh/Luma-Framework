#ifndef BLOOM
#define BLOOM 1
#endif

cbuffer _Globals : register(b0)
{
  row_major float4x4 g_SMapTM[4] : packoffset(c101);
  float4 g_DbgColor : packoffset(c117);
  float4 g_FilterTaps[8] : packoffset(c118);
  float4 g_FadingParams : packoffset(c126);
  float4 g_CSMRangesSqr : packoffset(c127);
  float2 g_SMapSize : packoffset(c128);
  float4 g_CameraOrigin : packoffset(c129);
  float4 IntensityWarmCol : packoffset(c0);
  float4 ColdCol : packoffset(c1);
}

SamplerState TMU0_Sampler_sampler_s : register(s0);
SamplerState TMU1_Sampler_sampler_s : register(s1);
Texture2D<float4> TMU0_Sampler : register(t0);
Texture2D<float4> TMU1_Sampler : register(t1);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 bloomColor = TMU1_Sampler.Sample(TMU1_Sampler_sampler_s, v1.xy).xyzw;
  float4 invBloomColor = 1.0 - bloomColor; // This could have been beyond 1 in vanilla too from float textures, but in both vanilla and luma, bloom is clamped to ~0.6 or so

  float4 sceneColor = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyzw;
  float4 emulatedSceneColor = sceneColor;
#if 1 // Luma: added saturate() to emulate vanilla UNORM behaviour, though however intuitive this might look, it makes bloom look more broken for extreme values, maybe because the double subtraction ends up mirrored again. Edit: actually it looks broken without this. // TODO: test again in the DLC that starts in an industrial area shootout.
  emulatedSceneColor = saturate(emulatedSceneColor);
#endif
  float4 invSceneColor = 1.0 - emulatedSceneColor;

  float4 someColorFactor = 1.0 - (invSceneColor * invBloomColor + emulatedSceneColor);

  float r1x = bloomColor.x - bloomColor.z; // Red - Blue???
  r1x = r1x * 100 + 0.5;
  float4 bloomFilter = 1.0;
  bloomFilter.xyz = IntensityWarmCol.x * (r1x * (-ColdCol.xyz + IntensityWarmCol.yzw) + ColdCol.xyz);

#if BLOOM
  o0.xyzw = sceneColor + (bloomFilter.xyzw * someColorFactor);
#else
  o0.xyzw = sceneColor;
#endif
}
