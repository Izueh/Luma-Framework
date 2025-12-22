cbuffer g_CameraPS_CB : register(b0)
{
  struct
  {
    float4 fs_projection_params;
    float4 fs_inverse_projection_xy;
    float4 fs_frustum_params;
    float4 fs_viewportScaleBias;
  } g_CameraPS : packoffset(c0);
}

cbuffer g_DX11AlphaTestPS_CB : register(b1)
{
  struct
  {
    float4 params;
  } g_DX11AlphaTestPS : packoffset(c0);
}

SamplerState texture1_ss_s : register(s1);
Texture2D<float4> texture1 : register(t1);

#define cmp

// MB is applied after all post processing, per object
// The result is quite jittery, but overall looks good
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = g_DX11AlphaTestPS.params.y + -g_DX11AlphaTestPS.params.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = v1.xy / v1.ww;
  r0.xy = r0.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  r0.xy = r0.xy * g_CameraPS.fs_viewportScaleBias.zw + g_CameraPS.fs_viewportScaleBias.xy;
  r1.xyzw = v3.xyxy / v3.zzzz;
  r2.xyzw = v2.xyxy / v2.zzzz;
  r1.xyzw = -r2.xyzw + r1.xyzw;
  r1.xyzw = g_CameraPS.fs_viewportScaleBias.zwzw * r1.xyzw;
  r0.xy = -r1.xy * float2(0.5,0.5) + r0.xy;
  r0.zw = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.zw).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.zw;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r2.xyz = r3.xyz + r2.xyz;
  r3.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xy = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r0.zw = r1.zw * float2(0.0909090936,0.0909090936) + r0.xy;
  r1.xyz = texture1.Sample(texture1_ss_s, r0.xy).xyz;
  r0.xyz = texture1.Sample(texture1_ss_s, r0.zw).xyz;
  r2.xyz = r3.xyz + r2.xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  o0.xyz = float3(0.0833333358,0.0833333358,0.0833333358) * r0.xyz;
  o0.w = 1;
}