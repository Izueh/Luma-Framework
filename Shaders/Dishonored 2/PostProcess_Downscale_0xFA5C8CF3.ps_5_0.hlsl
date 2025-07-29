cbuffer PerInstanceCB : register(b2)
{
  float4 cb_downscale_color : packoffset(c0);
  float2 cb_downscale_texelsize : packoffset(c1);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c1.z);
  float cb_env_autoexp_adapt_max_luminance : packoffset(c2);
  float cb_view_white_level : packoffset(c2.y);
}

// LUMA: added manually
cbuffer PerViewCB : register(b1)
{
   float4 cb_alwaystweak : packoffset(c0);
   float4 cb_viewrandom : packoffset(c1);
   float4x4 cb_viewprojectionmatrix : packoffset(c2);
   float4x4 cb_viewmatrix : packoffset(c6);
   float4 cb_subpixeloffset : packoffset(c10);
   float4x4 cb_projectionmatrix : packoffset(c11);
   float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
   float4x4 cb_previousviewmatrix : packoffset(c19);
   float4x4 cb_previousprojectionmatrix : packoffset(c23);
   float4 cb_mousecursorposition : packoffset(c27);
   float4 cb_mousebuttonsdown : packoffset(c28);
   float4 cb_jittervectors : packoffset(c29);
   float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
   float4x4 cb_inverseviewmatrix : packoffset(c34);
   float4x4 cb_inverseprojectionmatrix : packoffset(c38);
   float4 cb_globalviewinfos : packoffset(c42);
   float3 cb_wscamforwarddir : packoffset(c43);
   uint cb_alwaysone : packoffset(c43.w);
   float3 cb_wscamupdir : packoffset(c44);
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

SamplerState smp_bilinearclamp_s : register(s0);
Texture2D<float4> ro_downscale_bufferin : register(t0);

// LUMA: this does the first mip map for DoF, Bloom, Exposure etc
void main(
  float4 v0 : INTERP0,
  float4 v1 : INTERP1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  
  // LUMA: added dejittering of the first mip to fight of TAA jitters causing random temporal consequences in post processing
  r0.xyz = ro_downscale_bufferin.SampleLevel(smp_bilinearclamp_s, v0.xy - (cb_jittervectors.xy * float2(1, -1)), 0).xyz;
  r1.xyz = ro_downscale_bufferin.SampleLevel(smp_bilinearclamp_s, v0.zw - (cb_jittervectors.xy * float2(1, -1)), 0).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r1.xyz = ro_downscale_bufferin.SampleLevel(smp_bilinearclamp_s, v1.xy - (cb_jittervectors.xy * float2(1, -1)), 0).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r1.xyz = ro_downscale_bufferin.SampleLevel(smp_bilinearclamp_s, v1.zw - (cb_jittervectors.xy * float2(1, -1)), 0).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz); // LUMA: not really needed?
  o0.xyz = min(cb_env_autoexp_adapt_max_luminance, r0.xyz); // LUMA: not really needed?
  o0.w = 0;
  //o0.xyz = 0;
}