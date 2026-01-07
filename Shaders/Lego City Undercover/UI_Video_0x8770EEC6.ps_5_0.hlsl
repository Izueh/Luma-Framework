#include "../Includes/Common.hlsl"

cbuffer g_MiscGroupPS_CB : register(b0)
{
  struct
  {
    float4 fs_fog_color;
    float4 fs_liveCubemapReflectionPlane;
    float4 fs_envRotation0;
    float4 fs_envRotation1;
    float4 fs_envRotation2;
    float4 fs_exposure;
    float4 fs_screenSize;
    float4 fs_time;
    float4 fs_exposure2;
    float4 fs_fog_color2;
    float4 fs_per_pixel_fade_pos;
    float4 fs_per_pixel_fade_col;
    float4 fs_per_pixel_fade_rot;
    float4 fs_fog_params1;
    float4 fs_fog_params2;
    float4 fs_fog_params3;
    float4 fs_fog_params4;
  } g_MiscGroupPS : packoffset(c0);
}

SamplerState texture0_ss_s : register(s0);
SamplerState texture1_ss_s : register(s1);
SamplerState texture2_ss_s : register(s2);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 uv : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

#if 1
  float width, height;
  texture1.GetDimensions(width, height);
  float sourceAspectRatio = width / height;
#else // This is how the game would have originally appeared, independently of the video's actual aspect ratio, though we hope it's always 16:9, or if not, that it's not expected to be stretched anyway
  float sourceAspectRatio = 16.0 / 9.0;
#endif
  float targetAspectRatio = g_MiscGroupPS.fs_screenSize.x / g_MiscGroupPS.fs_screenSize.y;

  float2 scale = 1.0;

  if (targetAspectRatio >= sourceAspectRatio)
    scale.x = targetAspectRatio / sourceAspectRatio;
  else
    scale.y = sourceAspectRatio / targetAspectRatio;

  // Center the UVs before scaling them
  uv = (uv - 0.5) * scale + 0.5; 
  if (any(uv.xy < 0) || any(uv.xy > 1))
  {
    o0 = float4(0, 0, 0, v1.w); // Out of bounds UVs, draw black (and keep alpha because...)
    return;
  }

  r0.y = texture1.Sample(texture1_ss_s, uv.xy).x;
  r0.z = texture2.Sample(texture2_ss_s, uv.xy).x;
  r0.x = texture0.Sample(texture0_ss_s, uv.xy).x;
  r0.w = 1;
#if 1 // Luma: fix videos being decoded as BT.601 (full range), instead of BT.709 (full range) // TODO: test some more but it seems obvious
  r1.xyz = YUVtoRGB(r0.x, r0.y, r0.z, 0);
#else
  r1.y = dot(float4(1.16412354,-0.813476563,-0.391448975,0.529705048), r0.xyzw);
  r1.x = dot(float3(1.16412354,1.59579468,-0.87065506), r0.xyw);
  r1.z = dot(float3(1.16412354,2.01782227,-1.08166885), r0.xzw);
#endif
  r0.xyz = v1.xyz * r1.xyz;
  r0.xyz = g_MiscGroupPS.fs_exposure.xxx * r0.xyz;
  r1.xyz = r0.xyz * r0.xyz;
  o0.xyz = (0 < g_MiscGroupPS.fs_exposure.w) ? r0.xyz : r1.xyz;
  o0.w = v1.w;

  // Luma: add a light AutoHDR pass on videos
  if (LumaSettings.DisplayMode == 1)
  {
    o0.rgb = gamma_to_linear(o0.rgb, GCT_MIRROR);
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.UIPaperWhiteNits);
    o0.rgb = linear_to_gamma(o0.rgb, GCT_MIRROR);
  }
}