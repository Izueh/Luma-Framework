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
Texture2D<float4> layer0_sampler : register(t5);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float w2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = dot(-v1.xyz, -v1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = -v1.xyz * r0.xxx;
  r0.x = saturate(dot(g_MiscGroupPS.fs_fog_params3.xyz, r0.xyz));
  r0.x = r0.x * r0.x;
  r0.x = r0.x * r0.x;
  r0.x = 3 * r0.x;
  r0.xyz = r0.xxx * g_MiscGroupPS.fs_fog_params4.xyz + g_MiscGroupPS.fs_fog_params2.xyz;
  r0.xyz = g_MiscGroupPS.fs_fog_params1.xyz * r0.xyz;
  r0.w = 1 + -v1.w;
  r0.xyz = r0.xyz * r0.www;
  r1.xyzw = layer0_sampler.Sample(layer0_sampler_ss_s, v2.xy).xyzw;
  r1.xyzw = g_MaterialPS.fs_layer0_diffuse.xyzw * r1.xyzw;
  r1.xyz = r1.xyz * r1.xyz;
  o0.w = saturate(w2.x * r1.w);
  r0.xyz = r1.xyz * v1.www + r0.xyz;
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