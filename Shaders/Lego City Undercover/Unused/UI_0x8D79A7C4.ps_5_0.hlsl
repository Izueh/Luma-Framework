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

SamplerState texture0_ss_s : register(s0);
Texture2D<float4> texture0 : register(t0);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = texture0.Sample(texture0_ss_s, v2.xy).xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  r1.x = g_DX11AlphaTestPS.params.y * r0.w + -g_DX11AlphaTestPS.params.x;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r0.xyz = g_MiscGroupPS.fs_exposure.xxx * r0.xyz;
  o0.w = r0.w;
  r1.xyz = r0.xyz * r0.xyz;
  r0.w = cmp(0 < g_MiscGroupPS.fs_exposure.w);
  o0.xyz = r0.www ? r0.xyz : r1.xyz;

  // UNORM emulation
  o0.xyz = max(o0.xyz, 0.0);
  o0.a = saturate(o0.a);
}