// ---- Created with 3Dmigoto v1.3.16 on Wed Jul 30 00:06:57 2025

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

cbuffer g_LightsPS_CB : register(b1)
{

  struct
  {
    float4 fs_lightColor0;
    float4 fs_lightColor1;
    float4 fs_lightDirection0;
    float4 fs_lightDirection1;
    float4 fs_ReflectionTintDesatFactor;
  } g_LightsPS : packoffset(c0);

}

cbuffer g_MiscGroupPS_CB : register(b2)
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

SamplerState surface_sampler_ss_s : register(s5);
SamplerState envmap_samplerCube_ss_s : register(s6);
SamplerState legoSplatGun_sampler_ss_s : register(s7);
Texture2D<float4> surface_sampler : register(t5);
TextureCube<float4> envmap_samplerCube : register(t6);
Texture2D<float4> legoSplatGun_sampler : register(t7);



#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float2 v6 : TEXCOORD5,
  float4 v7 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v2.xyz, v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v2.xyz * r0.xxx;
  r0.w = dot(v1.xyz, v1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v1.xyz * r0.www;
  r2.xyz = surface_sampler.Sample(surface_sampler_ss_s, v6.xy).yzw;
  r2.xyz = r2.zxy * float3(2,2,2) + float3(-1,-1,-0);
  r2.xy = g_MaterialPS.fs_surface_params.xx * r2.xy;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r1.xyz = r2.yyy * r1.xyz;
  r0.xyz = r0.xyz * r2.xxx + r1.xyz;
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v5.xyz * r0.www;
  r0.xyz = r1.xyz * r2.zzz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = dot(g_LightsPS.fs_lightDirection0.xyz, r0.xyz);
  r0.w = max(0, r0.w);
  r1.xyz = g_LightsPS.fs_lightColor0.xyz * r0.www + v4.xyz;
  r1.w = dot(g_LightsPS.fs_lightDirection1.xyz, r0.xyz);
  r1.w = max(0, r1.w);
  r1.xyz = g_LightsPS.fs_lightColor1.xyz * r1.www + r1.xyz;
  r2.x = dot(-v3.xyz, -v3.xyz);
  r2.x = rsqrt(r2.x);
  r2.xyz = -v3.xyz * r2.xxx;
  r2.w = dot(r2.xyz, r0.xyz);
  r1.xyz = -r2.www * float3(1.25,1.25,1.25) + r1.xyz;
  r3.x = 1.25 * r2.w;
  r2.w = r2.w + r2.w;
  r0.xyz = r0.xyz * -r2.www + r2.xyz;
  r2.x = saturate(dot(g_MiscGroupPS.fs_fog_params3.xyz, r2.xyz));
  r2.x = r2.x * r2.x;
  r2.x = r2.x * r2.x;
  r2.x = 3 * r2.x;
  r2.xyz = r2.xxx * g_MiscGroupPS.fs_fog_params4.xyz + g_MiscGroupPS.fs_fog_params2.xyz;
  r2.xyz = g_MiscGroupPS.fs_fog_params1.xyz * r2.xyz;
  r1.xyz = r1.xyz * float3(0.400000006,0.400000006,0.400000006) + r3.xxx;
  r2.w = saturate(dot(-r0.xyz, g_LightsPS.fs_lightDirection1.xyz));
  r2.w = log2(r2.w);
  r2.w = g_MaterialPS.fs_specular_params.x * r2.w;
  r2.w = exp2(r2.w);
  r2.w = g_MaterialPS.fs_specular_params.w * r2.w;
  r1.w = r2.w * r1.w;
  r3.xyz = g_LightsPS.fs_lightColor1.xyz * r1.www;
  r1.w = saturate(dot(-r0.xyz, g_LightsPS.fs_lightDirection0.xyz));
  r1.w = log2(r1.w);
  r1.w = g_MaterialPS.fs_specular_params.x * r1.w;
  r1.w = exp2(r1.w);
  r1.w = g_MaterialPS.fs_specular_params.w * r1.w;
  r0.w = r1.w * r0.w;
  r3.xyz = g_LightsPS.fs_lightColor0.xyz * r0.www + r3.xyz;
  r0.w = 1 + -g_MaterialPS.fs_specular_params.z;
  r1.w = g_MaterialPS.fs_fresnel_params.y * r0.w;
  r0.w = -g_MaterialPS.fs_fresnel_params.y * r0.w + 1;
  r1.w = saturate(g_MaterialPS.fs_specular_specular.x * r1.w);
  r3.xyz = r1.www * r3.xyz;
  r4.xyz = g_MiscGroupPS.fs_envRotation1.xyz * -r0.yyy;
  r4.xyz = -r0.xxx * g_MiscGroupPS.fs_envRotation0.xyz + r4.xyz;
  r0.xyz = -r0.zzz * g_MiscGroupPS.fs_envRotation2.xyz + r4.xyz;
  r0.xyz = envmap_samplerCube.Sample(envmap_samplerCube_ss_s, r0.xyz).xyz;
  r0.xyz = g_MaterialPS.fs_envmap_params.yyy * r0.xyz;
  r0.xyz = r0.xyz * r0.xyz;
  r0.xyz = r0.xyz * r1.www;
  r4.xyz = -v7.xyz * float3(2,2,2) + g_MiscGroupPS.fs_per_pixel_fade_col.xyz;
  r5.xyz = v7.xyz + v7.xyz;
  r4.xyz = g_MiscGroupPS.fs_per_pixel_fade_col.www * r4.xyz + r5.xyz;
  r1.w = g_MiscGroupPS.fs_per_pixel_fade_pos.w * g_MiscGroupPS.fs_per_pixel_fade_pos.w + g_MiscGroupPS.fs_per_pixel_fade_pos.w;
  r6.xyz = g_MiscGroupPS.fs_per_pixel_fade_pos.xyz + -v3.xyz;
  r2.w = dot(r6.xyz, r6.xyz);
  r3.w = cmp(r2.w < r1.w);
  r1.w = r2.w / r1.w;
  r4.xyz = r3.www ? r4.xyz : r5.xyz;
  r2.w = 1 + -g_MiscGroupPS.fs_per_pixel_fade_col.w;
  r5.xyz = g_MaterialPS.fs_layer0_diffuse.xyz * r2.www + g_MiscGroupPS.fs_per_pixel_fade_col.www;
  r5.xyz = r3.www ? r5.xyz : g_MaterialPS.fs_layer0_diffuse.xyz;
  r4.xyz = r5.xyz * r4.xyz;
  r4.xyz = r4.xyz * r4.xyz;
  r0.xyz = r4.xyz * r0.www + r0.xyz;
  r0.xyz = r0.xyz * r1.xyz + r3.xyz;
  r0.w = dot(r6.xy, r6.xy);
  r0.w = rsqrt(r0.w);
  r1.xy = r6.xy * r0.ww;
  r3.x = dot(r1.xy, g_MiscGroupPS.fs_per_pixel_fade_rot.xy);
  r3.y = dot(r1.xy, g_MiscGroupPS.fs_per_pixel_fade_rot.zw);
  r1.xy = r3.xy * r1.ww;
  r1.xy = r1.xy * float2(0.300000012,0.300000012) + float2(0.5,0.5);
  r1.xyz = legoSplatGun_sampler.Sample(legoSplatGun_sampler_ss_s, r1.xy).xyz;
  r1.xyz = g_MiscGroupPS.fs_per_pixel_fade_col.www * r1.xyz;
  r0.xyz = r1.xyz * r1.xyz + r0.xyz;
  r0.w = 1 + -v1.w;
  r1.xyz = r2.xyz * r0.www;
  r0.xyz = r0.xyz * v1.www + r1.xyz;
  r0.xyz = g_MiscGroupPS.fs_exposure.yyy * r0.xyz;
  r1.xyz = sqrt(r0.xyz);
  r1.xyz = float3(1,1,1) / r1.xyz;
  r1.xyz = r1.xyz * abs(r0.xyz);
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r1.xyz : r0.xyz;
  o0.w = g_MaterialPS.fs_layer0_diffuse.w * v7.w;
  return;
}