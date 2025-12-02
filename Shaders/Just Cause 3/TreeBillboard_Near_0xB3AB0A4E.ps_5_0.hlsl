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
  linear noperspective centroid float4 v0 : SV_Position0,
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
  out float4 o3 : SV_Target3,
  out float oDepthLE : SV_DepthLessEqual)
{
  float4 r0,r1,r2,r3,r4,r5;
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
  r0.zw = v4.xy;
  r1.xyzw = AtlasTexture.SampleBias(AtlasSampler_s, r0.xyw, 0).xyzw;
  r2.xyzw = AtlasTexture.SampleBias(AtlasSampler_s, r0.xyz, 0).xyzw;
  r3.x = cmp(ALPHA_THRESHOLD >= r1.w);
  r1.w = r3.x ? 0 : r1.w;
  r3.xyz = v3.yxw;
  r3.xy = float2(1,1) + -r3.xy;
  r4.x = r3.x * r1.w;
  r4.x = v3.x * r4.x;
  r4.x = cmp(BLEND_THRESHOLD < r4.x);
  r1.xyzw = r4.xxxx ? r1.xyzw : 0;
  r4.x = r4.x ? v4.y : 0;
  r4.y = cmp(ALPHA_THRESHOLD >= r2.w);
  r2.w = r4.y ? 0 : r2.w;
  r3.x = r2.w * r3.x;
  r3.x = r3.x * r3.y;
  r3.x = cmp(BLEND_THRESHOLD < r3.x);
  r1.xyzw = r3.xxxx ? r2.xyzw : r1.xyzw;
  r2.x = r3.x ? v4.x : r4.x;
  r0.zw = v4.zw;
  r4.xyzw = AtlasTexture.SampleBias(AtlasSampler_s, r0.xyw, 0).xyzw;
  r5.xyzw = AtlasTexture.SampleBias(AtlasSampler_s, r0.xyz, 0).xyzw;
  r0.w = cmp(ALPHA_THRESHOLD >= r4.w);
  r4.w = r0.w ? 0 : r4.w;
  r0.w = v3.y * r4.w;
  r0.w = v3.x * r0.w;
  r0.w = cmp(BLEND_THRESHOLD < r0.w);
  r1.xyzw = r0.wwww ? r4.xyzw : r1.xyzw;
  r0.w = r0.w ? v4.w : r2.x;
  r2.x = cmp(ALPHA_THRESHOLD >= r5.w);
  r5.w = r2.x ? 0 : r5.w;
  r2.x = v3.y * r5.w;
  r2.x = r2.x * r3.y;
  r2.x = cmp(BLEND_THRESHOLD < r2.x);
  r1.xyzw = r2.xxxx ? r5.xyzw : r1.xyzw;
  r0.z = r2.x ? v4.z : r0.w;
  r0.xyzw = NormalMapAtlasTexture.SampleBias(AtlasSampler_s, r0.xyz, 0).xyzw;
  r0.xyzw = r0.xyzw * float4(2,2,2,2) + float4(-1,-1,-1,-1);
  r1.w = AlphaMulRef.x * r1.w + AlphaMulRef.y;
  o0.xyz = r1.xyz;
  r1.x = cmp(r1.w < 0);
  if (r1.x != 0) discard;
  o0.w = 1;
  r3.w = 0;
  r1.x = 0;
  r1.yzw = v3.zzw;
  r1.xy = r1.xy + -r3.zw;
  r2.x = dot(r0.zx, r1.xz);
  r2.z = dot(r0.zx, r1.yw);
  r2.y = r0.y;
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  r0.y = dot(v2.xyz, v2.xyz);
  r0.y = rsqrt(r0.y);
  r1.xyz = v2.xyz * r0.yyy;
  r3.xyz = v1.www * r1.xyz;
  r0.xyz = r2.xyz * r0.xxx + r3.xyz;
  r1.y = dot(r0.xyz, r0.xyz);
  r1.y = rsqrt(r1.y);
  r0.xyz = r1.yyy * r0.xyz;
  o1.xyz = r0.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  o1.w = 1;
  o2.xyzw = float4(0,0,0,0);
  r0.xy = -Globals[4].xz + v5.xz;
  r0.z = dot(r0.xy, r0.xy);
  r0.z = sqrt(r0.z);
  r0.z = 9.99999975e-005 + r0.z;
  r0.xy = r0.xy / r0.zz;
  r0.z = -SelfShadowSphereDistanceStart + r0.z;
  r0.z = max(0, r0.z);
  r0.z = saturate(SelfShadowSphereDistanceRcp * r0.z);
  r0.z = SelfShadowSphereInfluence * r0.z;
  r0.x = dot(r1.xz, r0.xy);
  r0.x = log2(abs(r0.x));
  r0.x = SelfShadowSpherePower * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.x = max(0, r0.x);
  r0.x = -1 + r0.x;
  r0.x = r0.z * r0.x + 1;
  r0.y = min(1, abs(r0.w));
  r0.z = r0.w * 0.5 + -0.5;
  r0.z = v2.w * r0.z;
  r1.xyz = r0.zzz * v6.xyz + v5.xyz;
  r0.y = -1 + r0.y;
  r0.y = SelfShadowDepthInfluence * r0.y + 1;
  r0.y = v8.x * r0.y;
  o3.z = r0.y * r0.x;
  o3.xyw = float3(0,0,0);
  r0.xy = ViewProjection._m12_m13 * r1.yy;
  r0.xy = r1.xx * ViewProjection._m02_m03 + r0.xy;
  r0.xy = r1.zz * ViewProjection._m22_m23 + r0.xy;
  r0.xy = ViewProjection._m32_m33 + r0.xy;
  r0.x = r0.x / r0.y;
  oDepthLE = min(v0.z, r0.x);

  // Luma: fix distant trees billboards appearing black/yellow due to negative values. Only "o3" is strictly necessary.
  // TODO: alternatively we could fix them when sampling the gbuffers matching "o2" and "o3" with a saturate in "0xCA7DFA32" and all other GBfuffers composition shaders. Delete this now cuz we did it with live patching?
  o0 = saturate(o0);
  o1 = saturate(o1);
  o2 = saturate(o2);
  o3 = saturate(o3);
  }