cbuffer ConstantBuffer : register(b0)
{
  float4 register0 : packoffset(c0);
  float4 register1 : packoffset(c1);
  float3 register2 : packoffset(c2);
  float4x4 register7 : packoffset(c3);
  float3 register11 : packoffset(c7);
  float3 register12 : packoffset(c8);
  float4x4 INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX : packoffset(c9);
  float4 ALPHA_TEST_PARAM : packoffset(c13);
  float4 TEXEL_MARGIN : packoffset(c14);
}

SamplerState SAMPLER_SKYMAP_blur_ddsSampler_s : register(s0);
SamplerState SAMPLER_CRIMESCENE_METAL_WJB_02_D_ddsSampler_s : register(s1);
SamplerState SAMPLER_CRIME_SCENE_TRAIN_LYQ_01_N_ddsSampler_s : register(s2);
SamplerState SAMPLER_AOSampler_s : register(s3);
SamplerState SAMPLER_qdAmbientCubemapSampler_s : register(s4);
TextureCube<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);
Texture2D<float4> texture3 : register(t3);
TextureCube<float4> texture4 : register(t4);

void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  float4 v2 : COLOR0,
  float4 v3 : TEXCOORD4,
  float4 v4 : COLOR1,
  float3 v5 : TEXCOORD5,
  float3 v6 : TEXCOORD6,
  float3 v7 : TEXCOORD7,
  float4 v8 : TEXCOORD2,
  float4 v9 : TEXCOORD3,
  float4 v10 : TEXCOORD8,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2)
{
  float4 r0,r1,r2,r3,r4;
  r0.x = texture3.Sample(SAMPLER_AOSampler_s, v0.zw).x;
  r0.yz = texture2.Sample(SAMPLER_CRIME_SCENE_TRAIN_LYQ_01_N_ddsSampler_s, v0.xy).yw;
  r1.xy = r0.zy * float2(2,2) + float2(-1,-1);
  r0.y = -r1.x * r1.x + 1;
  r0.y = -r1.y * r1.y + r0.y;
  r1.z = sqrt(abs(r0.y));
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = r1.xyz * r0.yyy;
  r1.x = dot(v5.xyz, v5.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = v5.xyz * r1.xxx;
  r1.w = dot(v6.xyz, v6.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = v6.xyz * r1.www;
  r1.w = dot(v7.xyz, v7.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = v7.xyz * r1.www;
  r1.x = dot(r1.xyz, r0.yzw);
  r1.y = dot(r2.xyz, r0.yzw);
  r1.z = dot(r3.xyz, r0.yzw);
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = r1.xyz * r0.yyy;
  r1.xyz = texture1.Sample(SAMPLER_CRIMESCENE_METAL_WJB_02_D_ddsSampler_s, v1.xy).xyz;
  r2.xyz = register2.xyz + -v3.xyz;
  r2.xyz = float3(0.00999999978,0.00999999978,0.00999999978) * r2.xyz;
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = r2.xyz * r1.www;
  r1.w = dot(r0.yzw, r2.xyz);
  r1.w = r1.w + r1.w;
  r2.xyz = r1.www * r0.yzw + -r2.xyz;
  r2.w = -r2.y;
  r3.xyz = texture0.Sample(SAMPLER_SKYMAP_blur_ddsSampler_s, r2.xwz).xyz;
  r3.xyz = r3.xyz * float3(1.5,1.5,1.5) + float3(-0.71073997,-0.644640982,-0.658554971);
  r3.xyz = r3.xyz * float3(0.644640028,0.644640028,0.644640028) + float3(0.71073997,0.644640982,0.658554971);
  r3.xyz = r3.xyz * r1.yyy;
  r1.xyz = v2.xyz * r1.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = float3(4,4,4) * r1.xyz;
  r1.w = dot(r0.yzw, register11.xyz);
  r1.w = max(0, r1.w);
  r3.xyz = register12.xyz * r1.www;
  r1.w = (register1.w < 0);
  if (r1.w != 0) {
    r1.w = (int)register1.x;
    r2.xyz = r1.www ? r2.xyz : r0.yzw;
    r2.w = -r2.y;
    r2.xyz = texture4.Sample(SAMPLER_qdAmbientCubemapSampler_s, r2.xwz).xyz;
  } else {
    r2.xyz = float3(0,0,0);
  }
  r4.xyz = register0.xyz * r0.xxx;
  r4.xyz = float3(1.39999998,1.39999998,1.39999998) * r4.xyz;
  r3.xyz = r3.xyz * r1.xyz;
  r3.xyz = r3.xyz * r0.xxx;
  r3.xyz = float3(1.39999998,1.39999998,1.39999998) * r3.xyz;
  r1.xyz = r4.xyz * r1.xyz + r3.xyz;
  r1.xyz = r1.xyz + r2.xyz;
  r1.xyz = -v4.xyz + r1.xyz;
  o0.xyz = v4.www * r1.xyz + v4.xyz;
  o2.x = v8.z / v8.w;
  o2.yzw = 0.0;
  r1.x = dot(r0.yzw, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m00_m10_m20);
  r1.y = dot(r0.yzw, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m01_m11_m21);
  r1.z = dot(r0.yzw, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m02_m12_m22);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx + float3(1,1,1);
  o1.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o0.w = 1;
  o1.w = 1;

  // Luma: lower bloom, it looks bad in HDR, it was extremely bright and out of place (in the first chapter with jayden)
  o0.w = 0.8;
}