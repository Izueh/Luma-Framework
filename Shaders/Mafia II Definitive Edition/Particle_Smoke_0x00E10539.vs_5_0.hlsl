cbuffer _Globals : register(b0)
{
  row_major float4x4 m001_ViewProjMat : packoffset(c0);
  float4 d002_CameraNearAndFarAndNearRelAndFarInv : packoffset(c5);
  float4 d004_CamWorldMatRight : packoffset(c6);
  float4 d005_CamWorldMatUp : packoffset(c7);
  float3 d006_CamWorldDir : packoffset(c8);
  row_major float4x4 m002_TexTransformMat[3] : packoffset(c17);
  float4 ucp0_ClipPlane : packoffset(c29);
  row_major float3x4 m000_WorldMat : packoffset(c49);
  float4 d007_PosDecompressionScaleAndOffset : packoffset(c56);
}

void main(
  uint4 v0 : POSITION0,
  uint4 v1 : TANGENT0,
  float3 v2 : BLENDWEIGHT0,
  float4 v3 : COLOR0,
  float4 v4 : COLOR1,
  float4 v5 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float4 o2 : TEXCOORD1,
  out float4 o3 : TEXCOORD2,
  out float4 o4 : TEXCOORD3,
  out float4 o5 : TEXCOORD4,
  out float4 o6 : TEXCOORD5,
  out float4 o7 : TEXCOORD6,
  out float4 o8 : SV_ClipDistance0)
{
  float4 r0,r1,r2,r3;
  r0.x = m000_WorldMat._m00;
  r0.y = m000_WorldMat._m10;
  r0.z = m000_WorldMat._m20;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = v2.x * r0.x;
  r0.y = cos(v2.y);
  r0.y = r0.x * r0.y;
  r0.yzw = d005_CamWorldMatUp.xyz * r0.yyy;
  r1.x = sin(-v2.y);
  r0.x = r1.x * r0.x;
  r0.xyz = r0.xxx * d004_CamWorldMatRight.xyz + r0.yzw;
  r1.xy = v1.xy; // utof
  r0.w = (r1.y >= 128.0);
  r0.w = r0.w ? -128.0 : -0.0;
  r1.z = r1.y + r0.w;
  r1.yw = d007_PosDecompressionScaleAndOffset.ww * float2(1,256);
  r2.z = dot(r1.xz, r1.yw);
  r3.xyzw = v0.xyzw; // utof
  r2.x = dot(r3.xy, r1.yw);
  r2.y = dot(r3.zw, r1.yw);
  r1.xyz = d007_PosDecompressionScaleAndOffset.xyz + r2.xyz;
  r1.w = 1;
  r2.x = dot(m000_WorldMat._m00_m01_m02_m03, r1.xyzw);
  r2.y = dot(m000_WorldMat._m10_m11_m12_m13, r1.xyzw);
  r2.z = dot(m000_WorldMat._m20_m21_m22_m23, r1.xyzw);
  r0.xyz = r2.xyz + r0.xyz;
  r0.w = 1;
  r1.x = dot(m001_ViewProjMat._m00_m01_m02_m03, r0.xyzw);
  r1.y = dot(m001_ViewProjMat._m10_m11_m12_m13, r0.xyzw);
  r1.z = dot(m001_ViewProjMat._m20_m21_m22_m23, r0.xyzw);
  r1.w = dot(m001_ViewProjMat._m30_m31_m32_m33, r0.xyzw);
  o0.xyzw = r1.xyzw;
  o8.xyzw = dot(r1.xyzw, ucp0_ClipPlane.xyzw);
  o1.w = d002_CameraNearAndFarAndNearRelAndFarInv.w * r1.w;
  o1.xyz = r0.xyz;
  o2.xy = v5.xy;
  o2.zw = float2(0,0);
#if 1 // Luma: fixed smoke flickering... This doesn't seem to have any other negative consequences (this particle never reaches 1 1 1 1 otherwise). There was some glitch in the vertex color calculations that made it overshoot for some vertices.
  v3.a = all(v3.rgba >= 1.0) ? 0.0 : v3.a;
#endif
  o3.xyzw = v3.zyxw;
  o4.xyzw = v4.zyxw;
  o5.x = dot(m002_TexTransformMat[0]._m00_m01_m02_m03, r0.xyzw);
  o5.y = dot(m002_TexTransformMat[0]._m10_m11_m12_m13, r0.xyzw);
  o5.z = dot(m002_TexTransformMat[0]._m20_m21_m22_m23, r0.xyzw);
  o5.w = dot(m002_TexTransformMat[0]._m30_m31_m32_m33, r0.xyzw);
  o6.x = dot(m002_TexTransformMat[1]._m00_m01_m02_m03, r0.xyzw);
  o6.y = dot(m002_TexTransformMat[1]._m10_m11_m12_m13, r0.xyzw);
  o6.z = dot(m002_TexTransformMat[1]._m20_m21_m22_m23, r0.xyzw);
  o6.w = dot(m002_TexTransformMat[1]._m30_m31_m32_m33, r0.xyzw);
  o7.x = dot(m002_TexTransformMat[2]._m00_m01_m02_m03, r0.xyzw);
  o7.y = dot(m002_TexTransformMat[2]._m10_m11_m12_m13, r0.xyzw);
  o7.z = dot(m002_TexTransformMat[2]._m20_m21_m22_m23, r0.xyzw);
  o7.w = dot(m002_TexTransformMat[2]._m30_m31_m32_m33, r0.xyzw);
}