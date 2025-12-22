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

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float4 v4 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.w = dot(g_LightsPS.fs_lightDirection0.xyz, r0.xyz);
  r0.x = dot(g_LightsPS.fs_lightDirection1.xyz, r0.xyz);
  r0.xy = max(float2(0,0), r0.xw);
  r0.yzw = g_LightsPS.fs_lightColor0.xyz * r0.yyy + v2.xyz;
  r0.xyz = g_LightsPS.fs_lightColor1.xyz * r0.xxx + r0.yzw;
  r1.xyz = g_MaterialPS.fs_layer0_diffuse.xyz * v4.xyz;
  r1.xyz = r1.xyz + r1.xyz;
  r1.xyz = r1.xyz * r1.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r0.w = dot(-v1.xyz, -v1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = -v1.xyz * r0.www;
  r0.w = saturate(dot(g_MiscGroupPS.fs_fog_params3.xyz, r1.xyz));
  r0.w = r0.w * r0.w;
  r0.w = r0.w * r0.w;
  r0.w = 3 * r0.w;
  r1.xyz = r0.www * g_MiscGroupPS.fs_fog_params4.xyz + g_MiscGroupPS.fs_fog_params2.xyz;
  r1.xyz = g_MiscGroupPS.fs_fog_params1.xyz * r1.xyz;
  r0.w = 1 + -v1.w;
  r1.xyz = r1.xyz * r0.www;
  r0.xyz = r0.xyz * v1.www + r1.xyz;
  r0.xyz = g_MiscGroupPS.fs_exposure.yyy * r0.xyz;
  r1.xyz = sqrt(r0.xyz);
  r1.xyz = float3(1,1,1) / r1.xyz;
  r1.xyz = r1.xyz * abs(r0.xyz);
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r1.xyz : r0.xyz;
  o0.w = g_MaterialPS.fs_layer0_diffuse.w * v4.w;

  // UNORM emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a);
}