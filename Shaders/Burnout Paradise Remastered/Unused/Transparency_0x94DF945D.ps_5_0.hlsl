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
SamplerState GlassFractureSampler_s : register(s14);
SamplerComparisonState shadowMapSamplerHighDetail_s : register(s15);
Texture2D<float4> DiffuseTextureSamplerTexture : register(t0);
TextureCube<float4> ReflectionTextureSamplerTexture : register(t13);
Texture2D<float4> GlassFractureSamplerTexture : register(t14);
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
  float4 v7 : TEXCOORD6,
  uint v8 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
  r0.xyzw = DiffuseTextureSamplerTexture.Sample(DiffuseTextureSampler_s, v3.xy).xyzw;
  r1.x = dot(v2.xyz, v2.xyz);
  r1.x = rsqrt(r1.x);
  r2.xyz = v2.xyz * r1.xxx;
  r1.y = saturate(dot(r2.xyz, -KeyLightDirection.xyz));
  r1.z = cmp(140 < v4.w);
  if (r1.z != 0) {
    r1.z = 0;
  } else {
    r1.w = cmp(v4.w < v5.w);
    r3.xyz = r1.www ? v4.xyz : v5.xyz;
    r1.w = -r1.y * r1.y + 1;
    r1.w = -0.000190000006 * r1.w;
    r1.w = min(-5.09999991e-005, r1.w);
    r1.w = -0.00200000009 + r1.w;
    r3.w = r1.w * ShadowMap_Constants2.z + r3.z;
    r4.xy = cmp(v4.ww < ShadowMap_Constants.yx);
    r5.xyz = r4.xxx ? ShadowMap_WorldToLight[1]._m00_m01_m02 : ShadowMap_WorldToLight[2]._m00_m01_m02;
    r6.xyz = r4.xxx ? ShadowMap_WorldToLight[1]._m30_m31_m32 : ShadowMap_WorldToLight[2]._m30_m31_m32;
    r7.xyz = r4.xxx ? ShadowMap_WorldToLight[1]._m11_m12_m10 : ShadowMap_WorldToLight[2]._m11_m12_m10;
    r4.xzw = r4.xxx ? ShadowMap_WorldToLight[1]._m22_m20_m21 : ShadowMap_WorldToLight[2]._m22_m20_m21;
    r5.xyz = r4.yyy ? ShadowMap_WorldToLight[0]._m00_m01_m02 : r5.xyz;
    r8.y = r4.y ? ShadowMap_WorldToLight[0]._m10 : r7.z;
    r7.xy = r4.yy ? ShadowMap_WorldToLight[0]._m11_m12 : r7.xy;
    r9.xz = r4.yy ? ShadowMap_WorldToLight[0]._m20_m21 : r4.zw;
    r10.z = r4.y ? ShadowMap_WorldToLight[0]._m22 : r4.x;
    r10.xyw = r4.yyy ? ShadowMap_WorldToLight[0]._m30_m31_m32 : r6.xyz;
    r4.y = ShadowMap_Constants3.x;
    r4.xzw = float3(0,0,1);
    r6.xyz = v5.xyz + r4.yzz;
    r4.xyz = v5.xyz + r4.xyz;
    r11.xyz = v5.xyz;
    r11.w = 1;
    r8.x = r5.x;
    r8.z = r9.x;
    r8.w = r10.x;
    r12.x = dot(r11.xyzw, r8.xyzw);
    r9.x = r5.y;
    r9.y = r7.x;
    r9.w = r10.y;
    r12.y = dot(r11.xyzw, r9.xyzw);
    r10.x = r5.z;
    r10.y = r7.y;
    r12.z = dot(r11.xyzw, r10.xyzw);
    r6.w = 1;
    r5.x = dot(r6.xyzw, r8.xyzw);
    r5.y = dot(r6.xyzw, r9.xyzw);
    r5.z = dot(r6.xyzw, r10.xyzw);
    r6.x = dot(r4.xyzw, r8.xyzw);
    r6.y = dot(r4.xyzw, r9.xyzw);
    r6.z = dot(r4.xyzw, r10.xyzw);
    r4.xyz = r5.xyz + -r12.xyz;
    r5.xyz = r6.xyz + -r12.xyz;
    r1.w = min(140, v4.w);
    r1.w = -r1.w * 0.00714285718 + 1;
    r1.w = 5 * r1.w;
    r1.w = dot(r1.ww, r1.ww);
    r6.xy = float2(0,0);
    r2.y = -1;
    while (true) {
      r3.z = cmp(1 < r2.y);
      if (r3.z != 0) break;
      r3.z = r2.y * r2.y;
      r7.xyz = r2.yyy * r5.xyz + r3.xyw;
      r8.xy = r6.xy;
      r8.z = -1;
      while (true) {
        r4.w = cmp(1 < r8.z);
        if (r4.w != 0) break;
        r4.w = r8.z * r8.z + r3.z;
        r4.w = -r4.w / r1.w;
        r4.w = 1.44269502 * r4.w;
        r4.w = exp2(r4.w);
        r9.xyz = r8.zzz * r4.xyz + r7.xyz;
        r5.w = shadowMapSamplerHighDetailTexture.SampleCmpLevelZero(shadowMapSamplerHighDetail_s, r9.xy, r9.z).x;
        r8.x = r4.w * r5.w + r8.x;
        r8.y = r8.y + r4.w;
        r8.z = 1 + r8.z;
      }
      r6.xy = r8.xy;
      r2.y = 1 + r2.y;
    }
    r1.w = 1 / r6.y;
    r1.z = saturate(r6.x * r1.w);
  }
  r1.w = -0.25 + r1.y;
  r1.w = saturate(20 * r1.w);
  r1.z = r1.z * r1.w;
  r1.w = dot(v1.xyz, v1.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = v1.xyz * r1.www;
  r1.y = r1.y * r1.z;
  r4.xyz = GlassFractureSamplerTexture.Sample(GlassFractureSampler_s, v7.xy).xyw;
  r5.xyz = GlassFractureSamplerTexture.Sample(GlassFractureSampler_s, v7.zw).xyw;
  r4.xyz = max(r5.xyz, r4.xyz);
  r4.xy = saturate(r4.xy * g_glassFractureStrength.yy + -g_glassFractureStrength.xx);
  r1.w = cmp(-0.05 >= -r4.x);
  r2.w = v2.y * r1.x + -r4.y;
  r1.x = dot(r2.xzw, r2.xzw);
  r1.x = rsqrt(r1.x);
  r2.xyz = r2.xwz * r1.xxx;
  r1.x = saturate(dot(r2.xyz, r3.xyz));
  r2.w = r1.x + r1.x;
  r2.xyz = r2.www * r2.xyz + -r3.xyz;
  r2.w = g_fresnelRanges.z + -g_fresnelRanges.w;
  r1.x = r1.x * r2.w + g_fresnelRanges.w;
  r3.xyz = ReflectionTextureSamplerTexture.Sample(ReflectionTextureSampler_s, r2.xyz).xyz;
  r2.w = 0.5 * FogColourPlusWhiteLevel.w;
  r2.w = g_reflectConstants.w * r2.w;
  r3.xyz = r3.xyz * g_reflectConstants.www + -r2.www;
  r3.xyz = FogColourPlusWhiteLevel.www * float3(0.5,0.5,0.5) + r3.xyz;
  r2.w = GetLuminance(r3.xyw); // Luma: fixed BT.601 luminance
  r3.xyz = r3.xyz + -r2.www;
  r3.xyz = g_reflectConstants.zzz * r3.xyz + r2.www;
  r2.x = saturate(dot(r2.xyz, -KeyLightDirection.xyz));
  r2.x = log2(r2.x);
  r2.x = g_reflectConstants.x * r2.x;
  r2.x = exp2(r2.x);
  r2.yzw = r4.xxx * float3(0.329999983,0.649999976,0.629999995) + float3(0.200000003,0.300000012,0.300000012);
  r0.xyz = materialDiffuse.xyz * r0.xyz;
  r2.yzw = r2.yzw * FogColourPlusWhiteLevel.www + -r0.xyz;
  r0.xyz = r1.www * r2.yzw + r0.xyz;
  r5.w = r1.w * r0.w + r0.w;
  r0.w = -r1.x * r1.w + r1.x;
  r1.x = r1.w * r4.z + r2.x;
  r2.xyz = KeyLightSpecularColour.xyz * r1.xxx;
  r1.xyw = KeyLightClampedColour.xyz * r1.yyy + v6.xyz;
  r1.xyw = r1.xyw * r5.www;
  r2.w = GetLuminance(r1.xyw); // Luma: fixed BT.601 luminance
  r2.w = saturate(v2.w * r2.w);
  r0.xyz = r1.xyw * r0.xyz;
  r1.x = -r0.w * 0.5 + 1;
  r1.yzw = r2.xyz * r1.zzz;
  r1.yzw = r3.xyz * r0.www + r1.yzw;
  r1.yzw = r1.yzw * r2.www;
  r0.xyz = r0.xyz * r1.xxx + r1.yzw;
  r5.xyz = r0.xyz * g_PerVehicleFog.www + g_PerVehicleFog.xyz;
  o0.xyzw = v8.xxxx ? r5.xyzw : float4(0,0,0,0.300000012);
}