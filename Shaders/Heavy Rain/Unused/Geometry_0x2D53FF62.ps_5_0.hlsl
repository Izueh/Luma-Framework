cbuffer ConstantBuffer : register(b0)
{
  float3 register0 : packoffset(c0);
  float4x4 register1 : packoffset(c1);
  float4x4 INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX : packoffset(c5);
  float4 ALPHA_TEST_PARAM : packoffset(c9);
  float4 TEXEL_MARGIN : packoffset(c10);
}

SamplerState SAMPLER_envmap2_LOW_ddsSampler_s : register(s0);
SamplerState SAMPLER_PHARE_D_02_ddsSampler_s : register(s1);
SamplerState SAMPLER_VEHICLE_BEAR_CAR_MACHINE_CH_02_N_DDSSampler_s : register(s2);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);

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
  float4 v9 : TEXCOORD8,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2)
{
  float4 r0,r1,r2;
  r0.xy = texture2.Sample(SAMPLER_VEHICLE_BEAR_CAR_MACHINE_CH_02_N_DDSSampler_s, v0.xy).yw;
  r0.xy = r0.yx * float2(2,2) + float2(-1,-1);
  r0.w = -r0.x * r0.x + 1;
  r0.w = -r0.y * r0.y + r0.w;
  r0.z = sqrt(abs(r0.w));
  r0.xyz = r0.xyz * float3(0.5,0.5,0.5) + float3(0,0,-0.5);
  r0.xyz = r0.xyz * float3(0.999979973,0.999979973,0.999979973) + float3(0,0,0.5);
  r0.xyz = r0.xyz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = dot(v5.xyz, v5.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v5.xyz * r0.www;
  r1.x = dot(r1.xyz, r0.xyz);
  r0.w = dot(v6.xyz, v6.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v6.xyz * r0.www;
  r1.y = dot(r2.xyz, r0.xyz);
  r0.w = dot(v7.xyz, v7.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v7.xyz * r0.www;
  r1.z = dot(r2.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx;
  r1.xyz = register0.xyz + -v3.xyz;
  r1.xyz = float3(0.00999999978,0.00999999978,0.00999999978) * r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.w = dot(r0.xyz, r1.xyz);
  r0.w = r0.w + r0.w;
  r1.xyz = r0.www * r0.xyz + -r1.xyz;
  r2.x = dot(r1.xyz, register1._m00_m10_m20);
  r2.y = dot(r1.xyz, register1._m01_m11_m21);
  r1.xy = r2.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r1.xyz = texture0.Sample(SAMPLER_envmap2_LOW_ddsSampler_s, r1.xy).xyz;
  r2.xyz = texture1.Sample(SAMPLER_PHARE_D_02_ddsSampler_s, v1.xy).xyz;
  r1.xyz = r1.xyz * float3(0.735520005,0.735520005,0.735520005) + r2.xyz;
  r1.xyz = float3(-1,-1,-1) + r1.xyz;
  r1.xyz = r1.xyz * v2.xyz + -v4.xyz;
  o0.xyz = v4.www * r1.xyz + v4.xyz;
  o0.w = 0;
  r1.x = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m00_m10_m20);
  r1.y = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m01_m11_m21);
  r1.z = dot(r0.xyz, INVERSE_TRANSPOSE_OBJECT_TO_VIEW_MATRIX._m02_m12_m22);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx + float3(1,1,1);
  o1.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o1.w = 1;
  o2.x = v8.z / v8.w;
  o2.yzw = 0.0;
  
  // LUMA: UNORM blends emulation
  o0.w = saturate(o0.w);
  o0.rgb = max(o0.rgb, 0.0);
}