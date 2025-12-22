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

cbuffer g_DX11AlphaTestPS_CB : register(b1)
{
  struct
  {
    float4 params;
  } g_DX11AlphaTestPS : packoffset(c0);
}

cbuffer g_DistFieldParamsPS_CB : register(b2)
{
  struct
  {
    float4 edgeParams;
    float4 glowColour;
  } g_DistFieldParamsPS : packoffset(c0);
}

SamplerState texture0_ss_s : register(s0);
Texture2D<float4> texture0 : register(t0);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = ddx_coarse(v2.x);
  r0.y = ddy_coarse(v2.y);
  r0.x = abs(r0.x) + abs(r0.y);
  r0.y = r0.x * g_DistFieldParamsPS.edgeParams.y + g_DistFieldParamsPS.edgeParams.x;
  r0.x = -r0.x * g_DistFieldParamsPS.edgeParams.y + g_DistFieldParamsPS.edgeParams.x;
  r0.y = r0.y + -r0.x;
  r0.y = 1 / r0.y;
  r0.zw = texture0.Sample(texture0_ss_s, v2.xy).xw;
  r0.x = r0.z * r0.w + -r0.x;
  r0.x = saturate(r0.x * r0.y);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r1.w = r0.y * r0.x;
  r2.xyz = float3(1,1,1) + -g_DistFieldParamsPS.glowColour.xyz;
  r2.xyz = r1.www * r2.xyz + g_DistFieldParamsPS.glowColour.xyz;
  r2.xyz = v1.xyz * r2.xyz;
  r0.xy = g_DistFieldParamsPS.edgeParams.xx + -g_DistFieldParamsPS.edgeParams.wz;
  r0.z = r0.z * r0.w + -r0.x;
  r0.x = r0.y + -r0.x;
  r0.x = 1 / r0.x;
  r0.x = saturate(r0.z * r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r2.w = r0.y * r0.x;
  r0.x = cmp(0 < g_DistFieldParamsPS.edgeParams.z);
  r1.xyz = v1.xyz;
  r0.xyzw = r0.xxxx ? r2.xyzw : r1.xyzw;
  r0.w = v1.w * r0.w;
  r0.xyz = g_MiscGroupPS.fs_exposure.xxx * r0.xyz;
  r1.x = g_DX11AlphaTestPS.params.y * r0.w + -g_DX11AlphaTestPS.params.x;
  o0.w = r0.w;
  r0.w = cmp(r1.x < 0);
  if (r0.w != 0) discard;
  r1.xyz = r0.xyz * r0.xyz;
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r0.xyz : r1.xyz;

  // UNORM emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a);
}