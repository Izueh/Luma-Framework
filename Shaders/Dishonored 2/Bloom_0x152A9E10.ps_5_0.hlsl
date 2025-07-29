cbuffer PerInstanceCB : register(b2)
{
  float4 cb_positiontoviewtexture : packoffset(c0);
  float4 cb_vectorfieldscales : packoffset(c1);
}

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
SamplerState smp_bilinearmirror_s : register(s1);
Texture2D<float4> ro_motionvectors : register(t0);
Texture2D<float4> ro_viewcolormap : register(t1);

#define cmp -

void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  v0.xy = cb_subpixeloffset.xy + v0.xy;
  v0.xy = cb_resolutionscale.xy * v0.xy;
  o0.xyzw = ro_viewcolormap.Sample(smp_bilinearmirror_s, v0.xy).xyzw;
  return;
  float4 r0,r1,r2,r3,r4;

  r0.xy = cb_subpixeloffset.xy + v0.xy;
  r0.xy = cb_resolutionscale.xy * r0.xy;
  r0.xyzw = ro_motionvectors.SampleLevel(smp_bilinearclamp_s, r0.xy, 0).xyzw;
  r0.zw = v0.xy + r0.zw;
  r1.xy = r0.xy * cb_vectorfieldscales.xx + r0.zw;
  r2.xyzw = cb_vectorfieldscales.xxxx * r0.xyxy;
  r2.xyzw = r2.xyzw * float4(2,2,3,3) + r0.zwzw;
  r0.xy = cb_resolutionscale.xy * r0.zw;
  r2.xyzw = cb_resolutionscale.xyxy * r2.xyzw;
  r0.zw = cb_resolutionscale.xy * r1.xy;
  r1.xy = cmp(float2(0,0) < r0.zw);
  r1.zw = cmp(r0.zw < float2(0,0));
  r1.xy = (int2)-r1.xy + (int2)r1.zw;
  r1.xy = (int2)r1.xy;
  r0.zw = r1.xy * r0.zw;
  r1.xyzw = -cb_positiontoviewtexture.zwzw * float4(0.5,0.5,0.5,0.5) + cb_resolutionscale.xyxy;
  r3.xy = min(r1.zw, r0.zw);
  r0.zw = r3.xy * float2(2,2) + -r0.zw;
  r3.xyzw = ro_viewcolormap.Sample(smp_bilinearmirror_s, r0.zw).xyzw;
  r0.zw = cmp(float2(0,0) < r0.xy);
  r4.xy = cmp(r0.xy < float2(0,0));
  r0.zw = (int2)-r0.zw + (int2)r4.xy;
  r0.zw = (int2)r0.zw;
  r0.xy = r0.xy * r0.zw;
  r0.zw = min(r0.xy, r1.zw);
  r0.xy = r0.zw * float2(2,2) + -r0.xy;
  r0.xyzw = ro_viewcolormap.Sample(smp_bilinearmirror_s, r0.xy).xyzw;
  r0.xyzw = r0.xyzw + r3.xyzw;
  r3.xyzw = cmp(float4(0,0,0,0) < r2.xyzw);
  r4.xyzw = cmp(r2.xyzw < float4(0,0,0,0));
  r3.xyzw = (int4)-r3.xyzw + (int4)r4.xyzw;
  r3.xyzw = (int4)r3.xyzw;
  r2.xyzw = r3.xyzw * r2.xyzw;
  r1.xyzw = min(r2.xyzw, r1.xyzw);
  r1.xyzw = r1.xyzw * float4(2,2,2,2) + -r2.xyzw;
  r2.xyzw = ro_viewcolormap.Sample(smp_bilinearmirror_s, r1.xy).xyzw;
  r1.xyzw = ro_viewcolormap.Sample(smp_bilinearmirror_s, r1.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  r0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  o0.w = saturate(r0.w);
  o0.xyz = r0.xyz;
}