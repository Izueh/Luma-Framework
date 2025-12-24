#include "../Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  float2 uv = v1.xy;
#if _30927F2E
  uv *= float2(0.5,1);
#endif

  r0.xyzw = t1.Sample(s1_s, uv).xyzw;
  r0.xy = float2(0.390625,1.984375) * r0.ww;
  r1.xyzw = t0.Sample(s0_s, uv).xyzw;
  r2.xyzw = t2.Sample(s2_s, uv).xyzw;

  // TODO: videos are interpreted as BT.709 limited range, make sure that was correct (it's actually more likely they were full range, and that this made them clip)
  r0.x = r1.w * 1.15625 + -r0.x;
  r0.y = r1.w * 1.15625 + r0.y;
  r1.z = -1.06861997 + r0.y;
  r0.x = -r2.w * 0.8125 + r0.x;
  r0.y = 1.59375 * r2.w;
  r0.y = r1.w * 1.15625 + r0.y;
  r1.xy = float2(-0.872539997,0.531369984) + r0.yx;
  r0.xyz = r1.xyz * float3(0.305306017,0.305306017,0.305306017) + float3(0.682171106,0.682171106,0.682171106);
  r0.xyz = r1.xyz * r0.xyz + float3(0.0125228781,0.0125228781,0.0125228781);
  o0.xyz = r1.xyz * r0.xyz;

#if 1 // Luma: these were drawing on non sRGB views in the UI phase, while the rest of the UI was drawing directly in linear on sRGB views. Luma upgrades the backbuffer to FLOAT so non sRGB views end up broken, and we need to manually linearize the output.
  o0.rgb = gamma_sRGB_to_linear(o0.xyz, GCT_MIRROR); // TODO: test... because bug reports claimed videos were darker than normal with luma, if it was an sRGB view mismatch, they would have been brighter (gamma space color viewed as linear space).
#endif

#if _30927F2E
  o0.w = t0.Sample(s0_s, uv + float2(0.5,0)).w;
#else
  o0.w = 1;
#endif
}