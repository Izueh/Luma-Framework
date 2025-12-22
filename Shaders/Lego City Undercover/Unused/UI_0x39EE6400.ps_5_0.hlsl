cbuffer g_MaterialPS_CB : register(b0)
{

  struct
  {
    float4 fs_layer0_diffuse;
    float4 fs_layer1_diffuse;
    float4 fs_layer2_diffuse;
    float4 fs_layer3_diffuse;
    float4 fs_specular_specular;
    float4 fs_specular2_specular;
    float4 fs_specular_params;
    float4 fs_surface_params;
    float4 fs_surface_params2;
    float4 fs_incandescentGlow;
    float4 fs_rimLightColour;
    float4 fs_fresnel_params;
    float4 fs_ambientColor;
    float4 fs_envmap_params;
    float4 fs_diffenv_params;
    float4 fs_refraction_color;
    float4 fs_refraction_kIndex;
    float4 fs_lego_params;
    float4 fs_vtf_kNormal;
    float4 fs_carpaint_params;
    float4 fs_brdf_params;
    float4 fs_fractal_params;
    float4 fs_carpaint_tints0;
    float4 fs_carpaint_tints1;
    float4 fs_specular3_specular;
    float4 fs_lego_brdf_fudge;
  } g_MaterialPS : packoffset(c0);

}

cbuffer g_MiscGroupPS_CB : register(b1)
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

SamplerState layer0_sampler_ss_s : register(s5);
SamplerState layer1_sampler_ss_s : register(s6);
Texture2D<float4> layer0_sampler : register(t5);
Texture2D<float4> layer1_sampler : register(t6);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float v2 : TEXCOORD1,
  float w2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = layer1_sampler.Sample(layer1_sampler_ss_s, v1.xy).xyzw;
  r1.x = -g_MaterialPS.fs_layer1_diffuse.w * r0.w + 1;
  r0.xyzw = g_MaterialPS.fs_layer1_diffuse.xyzw * r0.xyzw;
  r2.xyzw = layer0_sampler.Sample(layer0_sampler_ss_s, v1.zw).xyzw;
  r2.xyzw = g_MaterialPS.fs_layer0_diffuse.xyzw * r2.xyzw;
  r1.xyz = r2.xyz * r1.xxx;
  r0.xyz = r2.xyz * r0.xyz;
  r0.xyz = r0.xyz * r0.www + r1.xyz;
  r0.xyz = r0.xyz * r0.xyz;
  o0.w = saturate(w2.x * r2.w);
  r0.w = 1 + g_MaterialPS.fs_incandescentGlow.w;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = v2.xxx * r0.xyz;
  r0.xyz = g_MiscGroupPS.fs_exposure.yyy * r0.xyz;
  r1.xyz = sqrt(r0.xyz);
  r1.xyz = float3(1,1,1) / r1.xyz;
  r1.xyz = r1.xyz * abs(r0.xyz);
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r1.xyz : r0.xyz;

  // UNORM emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a);
}