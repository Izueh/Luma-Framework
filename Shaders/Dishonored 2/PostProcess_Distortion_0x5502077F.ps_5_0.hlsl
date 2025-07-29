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
  float4 cb_color : packoffset(c0);
  float4 cb_disto2_strength : packoffset(c1);
  float4 cb_disto1_strength : packoffset(c2);
  float4 cb_color_adjust : packoffset(c3);
  float2 cb_disto1_panningspeed : packoffset(c4);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c4.z);
  float2 cb_opacity_tilling : packoffset(c5);
  float2 cb_opacity_panningspeed : packoffset(c5.z);
  float2 cb_localtime : packoffset(c6);
  float2 cb_disto2_tilling : packoffset(c6.z);
  float2 cb_disto2_panningspeed : packoffset(c7);
  float2 cb_disto1_tilling : packoffset(c7.z);
  float cb_disto1_rotationspeed : packoffset(c8);
  float cb_postfx_screenquadfade : packoffset(c8.y);
  float cb_postfx_post_tonemap : packoffset(c8.z);
  float cb_particlealphatest : packoffset(c8.w);
  float cb_opacity_rotationspeed : packoffset(c9);
  float cb_material_seed : packoffset(c9.y);
  float cb_disto2_rotationspeed : packoffset(c9.z);
}

SamplerState smp_linearclamp_s : register(s0);
SamplerState smp_trilinearsampler_s : register(s1);
Texture2D<float4> ro_fx_disto1 : register(t0);
Texture2D<float4> ro_fx_disto2 : register(t1);
Texture2D<float4> ro_fx_opacity : register(t2);
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t3);

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;

  r0.x = v0.w * cb_material_seed + cb_localtime.x;
  r0.y = cb_disto2_rotationspeed * r0.x;
  sincos(r0.y, r1.x, r2.x);
  r0.yz = float2(-0.5,-0.5) + v0.xy;
  r1.yz = r0.yz * r2.xx;
  r0.w = r0.z * r1.x + r1.y;
  r1.x = -r0.y * r1.x + r1.z;
  r1.y = 0.5 + r1.x;
  r1.x = 0.5 + r0.w;
  r1.zw = cb_disto2_panningspeed.xy * r0.xx;
  r1.xy = r1.xy * cb_disto2_tilling.xy + r1.zw;
  r1.xy = ro_fx_disto2.Sample(smp_trilinearsampler_s, r1.xy).xy;
  r1.xy = cb_disto2_strength.xy * r1.xy;
  r0.w = cb_disto1_rotationspeed * r0.x;
  r1.zw = cb_disto1_panningspeed.xy * r0.xx;
  sincos(r0.w, r0.x, r2.x);
  r2.xy = r2.xx * r0.yz;
  r0.z = r0.z * r0.x + r2.x;
  r0.x = -r0.y * r0.x + r2.y;
  r0.y = 0.5 + r0.x;
  r0.x = 0.5 + r0.z;
  r0.xy = r0.xy * cb_disto1_tilling.xy + r1.zw;
  r0.xy = ro_fx_disto1.Sample(smp_trilinearsampler_s, r0.xy).xy;
  r0.xy = r0.xy * cb_disto1_strength.xy + r1.xy;
  r0.xy = v0.xy + r0.xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.z = cb_opacity_rotationspeed * cb_localtime.x;
  sincos(r0.z, r1.x, r2.x);
  r0.zw = r2.xx * r0.xy;
  r0.y = r0.y * r1.x + r0.z;
  r0.x = -r0.x * r1.x + r0.w;
  r1.xy = float2(0.5,0.5) + r0.yx;
  r0.xy = cb_localtime.xx * cb_opacity_panningspeed.xy;
  r0.xy = r1.xy * cb_opacity_tilling.xy + r0.xy;
  r0.x = ro_fx_opacity.Sample(smp_linearclamp_s, r0.xy).x;
  r0.y = -0.00196078443 + r0.x;
  r1.w = cb_color.w * r0.x;
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;
  r0.x = max(9.99999975e-005, cb_particlealphatest);
  r0.x = r1.w * cb_postfx_screenquadfade + -r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
  r0.y = cmp(0 < cb_postfx_post_tonemap);
  r0.x = r0.y ? 1 : r0.x;
  r0.yzw = cb_color_adjust.xyz * cb_color.xyz;
  r0.yzw = cb_color_adjust.www * r0.yzw;
  r1.xyz = r0.yzw / r0.xxx;
  r0.xyzw = cb_postfx_screenquadfade * r1.xyzw;
  o0.xyzw = r0.xyzw;
}