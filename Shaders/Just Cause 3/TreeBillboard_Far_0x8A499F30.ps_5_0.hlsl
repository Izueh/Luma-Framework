cbuffer booleans : register(b4)
{
  float2 AlphaMulRef : packoffset(c0);
}

cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

cbuffer Constants : register(b1)
{
  row_major float4x4 ViewProjection : packoffset(c0);
  float ALPHA_THRESHOLD : packoffset(c4);
  float BLEND_THRESHOLD : packoffset(c4.y);
  float SelfShadowSphereDistanceRcp : packoffset(c4.z);
  float SelfShadowDepthInfluence : packoffset(c4.w);
  float SelfShadowSphereInfluence : packoffset(c5);
  float SelfShadowSpherePower : packoffset(c5.y);
  float SelfShadowSphereDistanceStart : packoffset(c5.z);
  float b1_unused0 : packoffset(c5.w);
}

SamplerState AtlasSampler_s : register(s0);
Texture2DArray<float4> AtlasTexture : register(t0);
Texture2DArray<float4> NormalMapAtlasTexture : register(t1);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1;
  r0.xy = v8.yz / v8.ww;
  r0.zw = Globals[8].zw + Globals[8].zw;
  r0.xy = r0.xy / r0.zw;
  r0.x = dot(r0.xy, float2(0.467943996,-0.703647971));
  r0.x = frac(r0.x);
  r0.y = 1 + -v1.z;
  r0.x = r0.x + -r0.y;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = ddy_coarse(v1.xy);
  r0.xy = -v6.ww * r0.xy;
  r0.zw = ddx_coarse(v1.xy);
  r0.xy = r0.zw * v5.ww + r0.xy;
  r0.xy = v1.xy + r0.xy;
  r0.xy = max(v7.xz, r0.xy);
  r0.xy = min(v7.yw, r0.xy);
  r0.z = 0;
  r1.xyzw = AtlasTexture.SampleBias(AtlasSampler_s, r0.xyz, 0).xyzw;
  r0.x = NormalMapAtlasTexture.SampleBias(AtlasSampler_s, r0.xyz, 0).w;
  r0.x = r0.x * 2 + -1;
  r0.x = min(1, abs(r0.x));
  r0.x = -1 + r0.x;
  r0.x = SelfShadowDepthInfluence * r0.x + 1;
  r0.x = v8.x * r0.x;
  r0.y = cmp(BLEND_THRESHOLD >= r1.w);
  r1.xyzw = r0.yyyy ? float4(0,0,0,0) : r1.xyzw;
  r0.y = AlphaMulRef.x * r1.w + AlphaMulRef.y;
  o0.xyz = r1.xyz;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  o0.w = 1;
  r0.y = dot(v2.xyz, v2.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v2.xyz * r0.yyy;
  o1.xyz = r0.yzw * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  o1.w = 1;
  o2.xyzw = float4(0,0,0,0);
  r1.xy = -Globals[4].xz + v5.xz;
  r0.z = dot(r1.xy, r1.xy);
  r0.z = sqrt(r0.z);
  r0.z = 9.99999975e-005 + r0.z;
  r1.xy = r1.xy / r0.zz;
  r0.z = -SelfShadowSphereDistanceStart + r0.z;
  r0.z = max(0, r0.z);
  r0.z = saturate(SelfShadowSphereDistanceRcp * r0.z);
  r0.z = SelfShadowSphereInfluence * r0.z;
  r0.y = dot(r0.yw, r1.xy);
  r0.y = log2(abs(r0.y));
  r0.y = SelfShadowSpherePower * r0.y;
  r0.y = exp2(r0.y);
  r0.y = 1 + -r0.y;
  r0.y = max(0, r0.y);
  r0.y = -1 + r0.y;
  r0.y = r0.z * r0.y + 1;
  o3.z = r0.x * r0.y;
  o3.xyw = float3(0,0,0);

  // Luma: fix distant trees billboards appearing black/yellow due to negative values. Only "o3" is strictly necessary.
  o0 = saturate(o0);
  o1 = saturate(o1);
  o2 = saturate(o2);
  o3 = saturate(o3);
}