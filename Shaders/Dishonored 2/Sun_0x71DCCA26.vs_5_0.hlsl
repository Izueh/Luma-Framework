cbuffer PerInstanceCB : register(b2)
{
  float4 cb_atm_worldfog_sunspritecolor : packoffset(c0);
  float4 cb_godraysmask_transform2 : packoffset(c1);
  float4 cb_godraysmask_transform1 : packoffset(c2);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c3);
  float cb_view_white_level : packoffset(c3.z);
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

void main(
  float3 v0 : VERTEX_POSITION0,
  float2 v1 : VERTEX_TEXCOORD0,
  out float4 o0 : INTERP0,
  out float4 o1 : SV_POSITION0)
{
  // The sun in DH2 seems to scale with the horizontal FOV, but given that the game is Hor+ for Ultrawide support, that makes the size twice as big in ultrawide.
  // This scales it back to the size it has in 16:9 or roughly matching anyway.
  // Note that this still doesn't scale properly with the vertical FOV, so if you zoom in with a weapon, the sun keeps the same screen space size.
  float aspectRatioScale = (cb_globalviewinfos.x / cb_globalviewinfos.y) / (16.9 / 9.0); //TODO: wrong!

  float4 r0;
  o0.xy = v1.xy;
  o0.zw = float2(0,0);
  //TODO: fix other shaders that have "cb_godraysmask_transform1"?
  r0.xyzw = cb_godraysmask_transform1.xyzw * v0.xxyy / aspectRatioScale; // This scales it around its center
  r0.xy = r0.xy + r0.zw;
  o1.xy = cb_godraysmask_transform2.xy + r0.xy;
  o1.zw = float2(0,1);
}