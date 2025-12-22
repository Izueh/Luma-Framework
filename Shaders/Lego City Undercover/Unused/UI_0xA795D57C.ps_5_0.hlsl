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

SamplerState sceneEnvmap_samplerCube_ss_s : register(s3);
SamplerState surface_sampler_ss_s : register(s5);
SamplerState legoSplatGun_sampler_ss_s : register(s6);
TextureCube<float4> sceneEnvmap_samplerCube : register(t3);
Texture2D<float4> surface_sampler : register(t5);
Texture2D<float4> legoSplatGun_sampler : register(t6);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float3 v5 : TEXCOORD4,
  float2 v6 : TEXCOORD5,
  float4 v7 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = dot(v2.xyz, v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v2.xyz * r0.xxx;
  r0.w = dot(v1.xyz, v1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v1.xyz * r0.www;
  r2.xyz = surface_sampler.Sample(surface_sampler_ss_s, v6.xy).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-0);
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
  r0.w = dot(g_LightsPS.fs_lightDirection1.xyz, r0.xyz);
  r1.x = dot(-v3.xyz, -v3.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = -v3.xyz * r1.xxx;
  r1.w = dot(r0.xyz, r1.xyz);
  r2.x = r1.w + r1.w;
  r1.w = saturate(1 + -r1.w);
  r2.xyz = r0.xyz * -r2.xxx + r1.xyz;
  r0.x = dot(g_LightsPS.fs_lightDirection0.xyz, r0.xyz);
  r0.xw = max(float2(0,0), r0.xw);
  r0.y = saturate(dot(g_MiscGroupPS.fs_fog_params3.xyz, r1.xyz));
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y;
  r0.y = 3 * r0.y;
  r1.xyz = r0.yyy * g_MiscGroupPS.fs_fog_params4.xyz + g_MiscGroupPS.fs_fog_params2.xyz;
  r1.xyz = g_MiscGroupPS.fs_fog_params1.xyz * r1.xyz;
  r0.y = saturate(dot(-r2.xyz, g_LightsPS.fs_lightDirection1.xyz));
  r0.y = log2(r0.y);
  r0.y = g_MaterialPS.fs_specular_params.x * r0.y;
  r0.y = exp2(r0.y);
  r0.y = g_MaterialPS.fs_specular_params.w * r0.y;
  r0.y = r0.y * r0.w;
  r3.xyz = g_LightsPS.fs_lightColor1.xyz * r0.yyy;
  r0.y = saturate(dot(-r2.xyz, g_LightsPS.fs_lightDirection0.xyz));
  r0.y = log2(r0.y);
  r0.y = g_MaterialPS.fs_specular_params.x * r0.y;
  r0.y = exp2(r0.y);
  r0.y = g_MaterialPS.fs_specular_params.w * r0.y;
  r0.y = r0.y * r0.x;
  r4.xyz = g_LightsPS.fs_lightColor0.xyz * r0.xxx + v4.xyz;
  r0.xzw = g_LightsPS.fs_lightColor1.xyz * r0.www + r4.xyz;
  r3.xyz = g_LightsPS.fs_lightColor0.xyz * r0.yyy + r3.xyz;
  r4.xyz = g_MiscGroupPS.fs_envRotation1.xyz * -r2.yyy;
  r2.xyw = -r2.xxx * g_MiscGroupPS.fs_envRotation0.xyz + r4.xyz;
  r2.xyz = -r2.zzz * g_MiscGroupPS.fs_envRotation2.xyz + r2.xyw;
  r2.xyzw = sceneEnvmap_samplerCube.Sample(sceneEnvmap_samplerCube_ss_s, r2.xyz).xyzw;
  r0.y = 4 * r2.w;
  r2.w = -r2.w * 4 + 1;
  r2.xyz = r2.xyz * r2.xyz;
  r2.xyz = float3(4,4,4) * r2.xyz;
  r0.y = g_MaterialPS.fs_brdf_params.x * r2.w + r0.y;
  r2.xyz = r2.xyz * r0.yyy;
  r2.xyz = r2.xyz * r2.xyz + r3.xyz;
  r0.y = r1.w * r1.w;
  r0.y = r0.y * r0.y;
  r0.y = r1.w * r0.y;
  r1.w = 1 + -g_MaterialPS.fs_fresnel_params.y;
  r0.y = r1.w * r0.y + g_MaterialPS.fs_fresnel_params.y;
  r1.w = 1 + -g_MaterialPS.fs_specular_params.z;
  r2.w = r1.w * r0.y;
  r0.y = -r0.y * r1.w + 1;
  r1.w = saturate(g_MaterialPS.fs_specular_specular.x * r2.w);
  r2.xyz = r1.www * r2.xyz;
  r1.w = 1 + -g_MaterialPS.fs_brdf_params.y;
  r0.y = r1.w * r0.y + g_MaterialPS.fs_brdf_params.y;
  r3.xyz = -v7.xyz * float3(2,2,2) + g_MiscGroupPS.fs_per_pixel_fade_col.xyz;
  r4.xyz = v7.xyz + v7.xyz;
  r3.xyz = g_MiscGroupPS.fs_per_pixel_fade_col.www * r3.xyz + r4.xyz;
  r1.w = g_MiscGroupPS.fs_per_pixel_fade_pos.w * g_MiscGroupPS.fs_per_pixel_fade_pos.w + g_MiscGroupPS.fs_per_pixel_fade_pos.w;
  r5.xyz = g_MiscGroupPS.fs_per_pixel_fade_pos.xyz + -v3.xyz;
  r2.w = dot(r5.xyz, r5.xyz);
  r3.w = cmp(r2.w < r1.w);
  r1.w = r2.w / r1.w;
  r3.xyz = r3.www ? r3.xyz : r4.xyz;
  r2.w = 1 + -g_MiscGroupPS.fs_per_pixel_fade_col.w;
  r4.xyz = g_MaterialPS.fs_layer0_diffuse.xyz * r2.www + g_MiscGroupPS.fs_per_pixel_fade_col.www;
  r4.xyz = r3.www ? r4.xyz : g_MaterialPS.fs_layer0_diffuse.xyz;
  r3.xyz = r4.xyz * r3.xyz;
  r3.xyz = r3.xyz * r3.xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r0.xyz = r3.xyz * r0.xzw + r2.xyz;
  r0.w = dot(r5.xy, r5.xy);
  r0.w = rsqrt(r0.w);
  r2.xy = r5.xy * r0.ww;
  r3.x = dot(r2.xy, g_MiscGroupPS.fs_per_pixel_fade_rot.xy);
  r3.y = dot(r2.xy, g_MiscGroupPS.fs_per_pixel_fade_rot.zw);
  r2.xy = r3.xy * r1.ww;
  r2.xy = r2.xy * float2(0.300000012,0.300000012) + float2(0.5,0.5);
  r2.xyz = legoSplatGun_sampler.Sample(legoSplatGun_sampler_ss_s, r2.xy).xyz;
  r2.xyz = g_MiscGroupPS.fs_per_pixel_fade_col.www * r2.xyz;
  r0.xyz = r2.xyz * r2.xyz + r0.xyz;
  r0.w = 1 + -v1.w;
  r1.xyz = r1.xyz * r0.www;
  r0.xyz = r0.xyz * v1.www + r1.xyz;
  r0.xyz = g_MiscGroupPS.fs_exposure.yyy * r0.xyz;
  r1.xyz = sqrt(r0.xyz);
  r1.xyz = float3(1,1,1) / r1.xyz;
  r1.xyz = r1.xyz * abs(r0.xyz);
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r1.xyz : r0.xyz;
  o0.w = g_MaterialPS.fs_layer0_diffuse.w * v7.w;

  // UNORM emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a);
}