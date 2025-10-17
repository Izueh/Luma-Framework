cbuffer _Globals : register(b0)
{
  float4 SharedFogParameter0 : packoffset(c0);
  float4 SharedFogParameter1 : packoffset(c1);
  float4 SharedFogParameter2 : packoffset(c2);
  float4 SharedFogParameter3 : packoffset(c3);
  float bUseExponentialHeightFog : packoffset(c4);
  float4 HalfExponentialFogColorPlusLightInscatteringColor : packoffset(c5);
  float4 FogInScattering[4] : packoffset(c6);
  float4 FogMaxHeight : packoffset(c10);
  float4 MaxDensityDistance : packoffset(c11);
  float3 CameraWorldPosition : packoffset(c12);
  float FogMinStartDistance : packoffset(c12.w);
  float2 ShadowBlendParams : packoffset(c13);
}

cbuffer PSOffsetConstants : register(b2)
{
  float4 ScreenPositionScaleBias : packoffset(c0);
  float4 MinZ_MaxZRatio : packoffset(c1);
  float NvStereoEnabled : packoffset(c2);
}

SamplerState SceneDepthTexture_s : register(s0);
SamplerState AmbientShadowBuffer_s : register(s1);
Texture2D<float4> AmbientShadowBuffer : register(t0);
Texture2D<float4> SceneDepthTexture : register(t1);

#define cmp

// TODO: port fog fix from BS2
void main(
  float2 v0 : TEXCOORD0,
  float2 w0 : TEXCOORD1,
  float3 v1 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = SceneDepthTexture.SampleLevel(SceneDepthTexture_s, v0.xy, 0).x;
  r0.x = min(0.999000013, r0.x);
  r0.x = r0.x * MinZ_MaxZRatio.z + -MinZ_MaxZRatio.w;
  r0.x = 1 / r0.x;
  r0.y = dot(v1.xyz, v1.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v1.xyz * r0.yyy;
  r1.xyz = r0.yzw * r0.xxx;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = sqrt(r0.x);
  r0.x = -SharedFogParameter1.w + r0.x;
  r0.x = max(0, r0.x);
  r0.x = min(SharedFogParameter0.w, r0.x);
  r1.x = cmp(0.00999999978 < abs(r0.w));
  r1.x = r1.x ? r0.w : 0.00999999978;
  r0.y = dot(SharedFogParameter3.xyz, r0.yzw);
  r0.z = SharedFogParameter0.y * r1.x;
  r0.x = -r0.x * r0.z;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.x = SharedFogParameter0.x * r0.x;
  r0.x = r0.x / r0.z;
  r0.x = exp2(-r0.x);
  r0.x = min(1, r0.x);
  r0.z = 1 + -r0.x;
  r0.w = -SharedFogParameter0.z + r0.y;
  r0.w = saturate(SharedFogParameter3.w * r0.w);
  r0.w = r0.w * r0.w;
  r1.xyz = -HalfExponentialFogColorPlusLightInscatteringColor.xyz + SharedFogParameter2.xyz;
  r1.xyz = r0.www * r1.xyz + HalfExponentialFogColorPlusLightInscatteringColor.xyz;
  r0.w = 1 + r0.y;
  r0.y = cmp(r0.y < SharedFogParameter0.z);
  r0.w = saturate(SharedFogParameter2.w * r0.w);
  r2.xyz = HalfExponentialFogColorPlusLightInscatteringColor.xyz + -SharedFogParameter1.xyz;
  r2.xyz = r0.www * r2.xyz + SharedFogParameter1.xyz;
  r1.xyz = r0.yyy ? r2.xyz : r1.xyz;
  o0.xyz = r1.xyz * r0.zzz;
  r0.y = AmbientShadowBuffer.Sample(AmbientShadowBuffer_s, w0.xy).x;
  r0.y = 1 + -r0.y;
  r0.z = log2(abs(r0.y));
  r0.y = cmp(abs(r0.y) < 9.99999997e-007);
  r0.z = ShadowBlendParams.x * r0.z;
  r0.z = exp2(r0.z);
  r0.z = min(1, r0.z);
  r0.z = ShadowBlendParams.y * -r0.z + 1;
  r0.y = r0.y ? 1 : r0.z;
  o0.w = r0.x * r0.y;
#if 0 // Disable fog
  o0.xyz = 0;
#endif
}