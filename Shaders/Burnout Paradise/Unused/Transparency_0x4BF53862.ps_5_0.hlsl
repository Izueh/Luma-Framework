#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 sampleCoverage : packoffset(c0);
  row_major float4x4 viewProjection : packoffset(c1);
  row_major float4x4 ViewProjectionModified : packoffset(c5);
  row_major float4x4 ShadowMap_WorldToLight[3] : packoffset(c9);
  float4 ShadowMap_Constants : packoffset(c21);
  float4 ShadowMap_Constants2 : packoffset(c22);
  float4 ShadowMap_Constants3 : packoffset(c23);
  float4 ShadowMap_ObjectCsmSelect : packoffset(c24);
  float4 ScattCoeffs : packoffset(c25);
  float4 FogColourPlusWhiteLevel : packoffset(c26);
  row_major float4x4 IrradianceQuadricA : packoffset(c27);
  row_major float4x4 IrradianceQuadricB : packoffset(c31);
  float3 KeyLightDirection : packoffset(c35);
  float3 ViewPosition : packoffset(c36);
  float3 KeyLightSpecularColour : packoffset(c37);
  float3 KeyLightClampedColour : packoffset(c38);
  row_major float4x4 worldViewProj : packoffset(c39);
  row_major float4x4 world : packoffset(c43);
  float4 g_verletOffsets[128] : packoffset(c47);
  float4 g_glassFractureStrength : packoffset(c175);
  float4 g_glassFractureUVOffsets : packoffset(c176);
  float4 g_glassFractureFresnelRanges : packoffset(c177) = {0,1,0,0};
  float4 g_PerVehicleFog : packoffset(c178) = {0,0,0,0};
  float4 g_fresnelRanges : packoffset(c179) = {1,2,0.5,1};
  float4 g_reflectConstants : packoffset(c180) = {32,1,1,1};
  float4 g_specularConstants : packoffset(c181) = {0.995999992,2,0.5,1};
  float4 materialDiffuse : packoffset(c182) = {1,1,1,1};
}

SamplerState DiffuseTextureSampler_s : register(s0);
SamplerState ReflectionTextureSampler_s : register(s13);
SamplerComparisonState shadowMapSamplerHighDetail_s : register(s15);
Texture2D<float4> DiffuseTextureSamplerTexture : register(t0);
TextureCube<float4> ReflectionTextureSamplerTexture : register(t13);
Texture2D<float4> shadowMapSamplerHighDetailTexture : register(t15);

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float3 v6 : TEXCOORD5,
  uint v7 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  r0.xyzw = DiffuseTextureSamplerTexture.Sample(DiffuseTextureSampler_s, v3.xy).xyzw;
  r1.x = dot(v2.xyz, v2.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = v2.xyz * r1.xxx;
  r1.w = saturate(dot(r1.xyz, -KeyLightDirection.xyz));
  r2.x = cmp(140 < v4.w);
  if (r2.x != 0) {
    r2.x = 0;
  } else {
    r2.y = cmp(v4.w < v5.w);
    r3.xyz = r2.yyy ? v4.xyz : v5.xyz;
    r2.y = -r1.w * r1.w + 1;
    r2.y = -0.000190000006 * r2.y;
    r2.y = min(-5.09999991e-005, r2.y);
    r2.y = -0.00200000009 + r2.y;
    r3.w = r2.y * ShadowMap_Constants2.z + r3.z;
    r2.yz = cmp(v4.ww < ShadowMap_Constants.yx);
    r4.xyz = r2.yyy ? ShadowMap_WorldToLight[1]._m00_m01_m02 : ShadowMap_WorldToLight[2]._m00_m01_m02;
    r5.xyz = r2.yyy ? ShadowMap_WorldToLight[1]._m30_m31_m32 : ShadowMap_WorldToLight[2]._m30_m31_m32;
    r6.xyz = r2.yyy ? ShadowMap_WorldToLight[1]._m11_m12_m10 : ShadowMap_WorldToLight[2]._m11_m12_m10;
    r7.xyz = r2.yyy ? ShadowMap_WorldToLight[1]._m22_m20_m21 : ShadowMap_WorldToLight[2]._m22_m20_m21;
    r4.xyz = r2.zzz ? ShadowMap_WorldToLight[0]._m00_m01_m02 : r4.xyz;
    r8.y = r2.z ? ShadowMap_WorldToLight[0]._m10 : r6.z;
    r2.yw = r2.zz ? ShadowMap_WorldToLight[0]._m11_m12 : r6.xy;
    r6.xz = r2.zz ? ShadowMap_WorldToLight[0]._m20_m21 : r7.yz;
    r7.z = r2.z ? ShadowMap_WorldToLight[0]._m22 : r7.x;
    r7.xyw = r2.zzz ? ShadowMap_WorldToLight[0]._m30_m31_m32 : r5.xyz;
    r5.y = ShadowMap_Constants3.x;
    r5.xzw = float3(0,0,1);
    r9.xyz = v5.xyz + r5.yzz;
    r5.xyz = v5.xyz + r5.xyz;
    r10.xyz = v5.xyz;
    r10.w = 1;
    r8.x = r4.x;
    r8.z = r6.x;
    r8.w = r7.x;
    r11.x = dot(r10.xyzw, r8.xyzw);
    r6.x = r4.y;
    r6.y = r2.y;
    r6.w = r7.y;
    r11.y = dot(r10.xyzw, r6.xyzw);
    r7.x = r4.z;
    r7.y = r2.w;
    r11.z = dot(r10.xyzw, r7.xyzw);
    r9.w = 1;
    r4.x = dot(r9.xyzw, r8.xyzw);
    r4.y = dot(r9.xyzw, r6.xyzw);
    r4.z = dot(r9.xyzw, r7.xyzw);
    r8.x = dot(r5.xyzw, r8.xyzw);
    r8.y = dot(r5.xyzw, r6.xyzw);
    r8.z = dot(r5.xyzw, r7.xyzw);
    r2.yzw = r4.xyz + -r11.xyz;
    r4.xyz = r8.xyz + -r11.xyz;
    r3.z = min(140, v4.w);
    r3.z = -r3.z * 0.00714285718 + 1;
    r3.z = 5 * r3.z;
    r3.z = dot(r3.zz, r3.zz);
    r5.xy = float2(0,0);
    r4.w = -1;
    while (true) {
      r5.z = cmp(1 < r4.w);
      if (r5.z != 0) break;
      r5.z = r4.w * r4.w;
      r6.xyz = r4.www * r4.xyz + r3.xyw;
      r7.xy = r5.xy;
      r7.z = -1;
      while (true) {
        r5.w = cmp(1 < r7.z);
        if (r5.w != 0) break;
        r5.w = r7.z * r7.z + r5.z;
        r5.w = -r5.w / r3.z;
        r5.w = 1.44269502 * r5.w;
        r5.w = exp2(r5.w);
        r8.xyz = r7.zzz * r2.yzw + r6.xyz;
        r6.w = shadowMapSamplerHighDetailTexture.SampleCmpLevelZero(shadowMapSamplerHighDetail_s, r8.xy, r8.z).x;
        r7.x = r5.w * r6.w + r7.x;
        r7.y = r7.y + r5.w;
        r7.z = 1 + r7.z;
      }
      r5.xy = r7.xy;
      r4.w = 1 + r4.w;
    }
    r2.y = 1 / r5.y;
    r2.x = saturate(r5.x * r2.y);
  }
  r2.y = -0.25 + r1.w;
  r2.y = saturate(20 * r2.y);
  r2.x = r2.x * r2.y;
  r2.y = dot(v1.xyz, v1.xyz);
  r2.y = rsqrt(r2.y);
  r2.yzw = v1.xyz * r2.yyy;
  r1.w = r2.x * r1.w;
  r3.x = saturate(dot(r1.xyz, r2.yzw));
  r3.y = r3.x + r3.x;
  r1.xyz = r3.yyy * r1.xyz + -r2.yzw;
  r2.y = g_fresnelRanges.z + -g_fresnelRanges.w;
  r2.y = r3.x * r2.y + g_fresnelRanges.w;
  r3.xyz = ReflectionTextureSamplerTexture.Sample(ReflectionTextureSampler_s, r1.xyz).xyz;
  r2.z = 0.5 * FogColourPlusWhiteLevel.w;
  r2.z = g_reflectConstants.w * r2.z;
  r3.xyz = r3.xyz * g_reflectConstants.www + -r2.zzz;
  r3.xyz = FogColourPlusWhiteLevel.www * float3(0.5,0.5,0.5) + r3.xyz;
  r2.z = GetLuminance(r3.xyz); // Luma: fixed BT.601 luminance
  r3.xyz = r3.xyz + -r2.zzz;
  r3.xyz = g_reflectConstants.zzz * r3.xyz + r2.zzz;
  r1.x = saturate(dot(r1.xyz, -KeyLightDirection.xyz));
  r1.x = log2(r1.x);
  r1.x = g_reflectConstants.x * r1.x;
  r1.x = exp2(r1.x);
  r4.xyz = materialDiffuse.xyz * r0.xyz;
  r1.xyz = KeyLightSpecularColour.xyz * r1.xxx;
  r5.xyz = KeyLightClampedColour.xyz * r1.www + v6.xyz;
  r5.xyz = r5.xyz * r0.www;
  r1.w = GetLuminance(r5.xyz); // Luma: fixed BT.601 luminance
  r1.w = saturate(v2.w * r1.w);
  r4.xyz = r5.xyz * r4.xyz;
  r2.z = -r2.y * 0.5 + 1;
  r1.xyz = r1.xyz * r2.xxx;
  r1.xyz = r3.xyz * r2.yyy + r1.xyz;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = r4.xyz * r2.zzz + r1.xyz;
  r0.xyz = r1.xyz * g_PerVehicleFog.www + g_PerVehicleFog.xyz;
  o0.xyzw = v7.xxxx ? r0.xyzw : float4(0,0,0,0.300000012);
}