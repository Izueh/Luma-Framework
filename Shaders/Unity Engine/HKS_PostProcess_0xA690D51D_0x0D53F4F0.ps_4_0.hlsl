#include "../Includes/Common.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// Luma: this runs immediately after vignette
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  float w, h;
  t1.GetDimensions(w, h);
  float ar = w / h;
  
  float dither = 0.0;
#if _17BA635D // New game version added dither (anti banding), which is only active when enabling dithering in the settings
  r0.x = dot(v0.xy, float2(0.0671105608,0.00583714992));
  r0.x = frac(r0.x);
  r0.x = 52.9829178 * r0.x;
  r0.x = frac(r0.x);
  dither = r0.x * 0.00392156886 + -0.00196078443;
#endif

  // TODO: figure out what this is
  float2 uv2 = v2.xy;
  float ndc2 = (uv2.x - 0.5) * 2.0;
  ndc2 = pow(abs(ndc2), max(ar / (21.0 / 9.0), 1.0)) * sign(ndc2); // Adjust by aspect ratio as vignette was extremely stretched at 32:9, we do it at 21:9 because the game supports that, and scaling it from 16:9 seems too much
  uv2.x = (ndc2 / 2.0) + 0.5;

  r0.x = v1.w * -2.0 + 1.0;
  r0.yz = v3.xy / v3.w;
  r1.xyzw = t1.Sample(s1_s, r0.yz).xyzw; // Darkness effect
  r0.x = r1.z * r0.x + v1.w;
  r2.xyzw = t0.Sample(s0_s, uv2).xyzw; // Some vignette like effect
#if TEST // Test: print purple
  if (any((abs(r2.rgb) - 0.0) >= 0.001) || (abs(r2.a) - 0.0) < 0.001)
  {
    o0 = float4(1, 0, 1, 1); return;
  }
#endif
  r0.x = saturate(r2.w * r0.x + r1.y);
  r0.yzw = v1.xyz * r2.xyz;
  o0.xyz = r0.yzw * r0.x;
  o0.w = r0.x;

  o0 += dither;
}