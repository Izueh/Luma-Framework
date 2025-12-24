cbuffer _Globals : register(b0)
{
  float4 c130_GlobalSceneParams : packoffset(c15);
  float4 n005_RenderTargetSizesInv : packoffset(c9);
  float4 c000_AmbientColor : packoffset(c23);
  float4 c001_AmbientColor2 : packoffset(c24);

  struct
  {
    float4 ColorAndAttenuation;
    float4 PosOrDirAndFar;
    float4 Switches;
    float4 ShadowTrans;
  } d000_Lights[3] : packoffset(c28);

  float c040_AlphaRefVal : packoffset(c64);
  float c041_AlphaTestBool : packoffset(c65);
  float2 D013_SpecularPowerAndLevel : packoffset(c66);
  float4 D067_DistToMaxOpacityInv : packoffset(c67);
  float D350_ForcedWorldNormalZ : packoffset(c68);
  float4 c025_VisualColorModulator : packoffset(c99);
}

SamplerState s040_DepthTexture_sampler_s : register(s0);
SamplerState s110_DynamicAOTexture_sampler_s : register(s1);
SamplerState s500__sampler_s : register(s2);
SamplerState s510__sampler_s : register(s3);
SamplerState s520__sampler_s : register(s4);
SamplerState S000_DiffuseTexture_sampler_s : register(s8);
Texture2D<float4> s040_DepthTexture : register(t0);
Texture2D<float4> s110_DynamicAOTexture : register(t1);
Texture2D<float4> s500_ : register(t2);
Texture2D<float4> s510_ : register(t3);
Texture2D<float4> s520_ : register(t4);
Texture2D<float4> S000_DiffuseTexture : register(t8);

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
  out float4 o0 : SV_Target0)
{
#if 0 // Disabled: flicker still happens
  v3.xyzw = saturate(v3.xyzw);
  v3.xyzw = v3.w >= 0.999 ? 0.0 : v3.xyzw;
  o0.xyzw = v3.xyzw;

  o0.xyz = 1;
  o0.w = v3.w;
  
  return;
#elif 0 // Luma: fixed smoke flickering... Moved to VS
  v3.a = v3.a >= 0.999 ? 0.0 : v3.a;
#endif
  float4 r0,r1,r2,r3;
  r0.xy = v5.xy / v5.ww;
  r0.x = s500_.Sample(s500__sampler_s, r0.xy).x;
  r0.y = cmp(0 >= -v5.z);
  r0.y = r0.y ? 1.000000 : 0;
  r0.x = r0.x * r0.y;
  r0.xyz = d000_Lights[0].ColorAndAttenuation.xyz * r0.xxx;
  r1.xyz = -v1.xyz * d000_Lights[0].Switches.zzz + d000_Lights[0].PosOrDirAndFar.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = sqrt(r0.w);
  r1.xy = -d000_Lights[0].Switches.zx + float2(1,1);
  r1.x = v5.z * r1.x;
  r0.w = r0.w * d000_Lights[0].Switches.z + r1.x;
  r1.x = -d000_Lights[0].ColorAndAttenuation.w * r0.w + 1;
  r0.w = cmp(d000_Lights[0].PosOrDirAndFar.w >= r0.w);
  r0.w = r0.w ? 1.000000 : 0;
  r0.xyz = r1.xxx * r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r1.xz = v6.xy / v6.ww;
  r0.w = s510_.Sample(s510__sampler_s, r1.xz).x;
  r1.x = cmp(0 >= -v6.z);
  r1.x = r1.x ? 1.000000 : 0;
  r0.w = r1.x * r0.w;
  r1.xzw = d000_Lights[1].ColorAndAttenuation.xyz * r0.www;
  r2.xyz = -v1.xyz * d000_Lights[1].Switches.zzz + d000_Lights[1].PosOrDirAndFar.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = sqrt(r0.w);
  r2.xy = -d000_Lights[1].Switches.zx + float2(1,1);
  r2.x = v6.z * r2.x;
  r0.w = r0.w * d000_Lights[1].Switches.z + r2.x;
  r2.x = -d000_Lights[1].ColorAndAttenuation.w * r0.w + 1;
  r0.w = cmp(d000_Lights[1].PosOrDirAndFar.w >= r0.w);
  r0.w = r0.w ? 1.000000 : 0;
  r1.xzw = r2.xxx * r1.xzw;
  r1.xzw = r1.xzw * r0.www;
  r0.w = saturate(dot(float3(0.212500006,0.715399981,0.0720999986), r1.xzw));
  r2.xz = v0.xy * n005_RenderTargetSizesInv.zw + n005_RenderTargetSizesInv.xy;
  r2.w = s110_DynamicAOTexture.Sample(s110_DynamicAOTexture_sampler_s, r2.xz).w;
  r2.x = s040_DepthTexture.Sample(s040_DepthTexture_sampler_s, r2.xz).x;
  r2.x = -v1.w + r2.x;
  r2.x = saturate(D067_DistToMaxOpacityInv.x * r2.x);
  r2.z = 1 + -r2.w;
  r2.y = r2.y * r2.z;
  r0.w = r2.y * r0.w + r2.w;
  r1.xzw = r1.xzw * r0.www;
  r0.w = saturate(dot(float3(0.212500006,0.715399981,0.0720999986), r0.xyz));
  r1.y = r2.z * r1.y;
  r0.w = r1.y * r0.w + r2.w;
  r0.xyz = r0.xyz * r0.www + r1.xzw;
  r1.xy = v7.xy / v7.ww;
  r0.w = s520_.Sample(s520__sampler_s, r1.xy).x;
  r1.x = cmp(0 >= -v7.z);
  r1.x = r1.x ? 1.000000 : 0;
  r0.w = r1.x * r0.w;
  r1.xyz = d000_Lights[2].ColorAndAttenuation.xyz * r0.www;
  r3.xyz = -v1.xyz * d000_Lights[2].Switches.zzz + d000_Lights[2].PosOrDirAndFar.xyz;
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = sqrt(r0.w);
  r3.xy = -d000_Lights[2].Switches.zx + float2(1,1);
  r1.w = v7.z * r3.x;
  r2.y = r3.y * r2.z;
  r0.w = r0.w * d000_Lights[2].Switches.z + r1.w;
  r1.w = -d000_Lights[2].ColorAndAttenuation.w * r0.w + 1;
  r0.w = cmp(d000_Lights[2].PosOrDirAndFar.w >= r0.w);
  r0.w = r0.w ? 1.000000 : 0;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = r1.xyz * r0.www;
  r0.w = saturate(dot(float3(0.212500006,0.715399981,0.0720999986), r1.xyz));
  r0.w = r2.y * r0.w + r2.w;
  r0.xyz = r1.xyz * r0.www + r0.xyz;
  r0.w = D350_ForcedWorldNormalZ + 1;
  r0.w = 0.5 * r0.w;
  r1.xyz = c001_AmbientColor2.xyz + -c000_AmbientColor.xyz;
  r1.xyz = r0.www * r1.xyz + c000_AmbientColor.xyz;
  r0.xyz = r1.xyz * r2.www + r0.xyz;
  r1.xyz = v4.xyz;
  r1.w = 0;
  r3.xyzw = S000_DiffuseTexture.Sample(S000_DiffuseTexture_sampler_s, v2.xy).xyzw;
  r3.xyzw = v3.xyzw * r3.xyzw;
  r2.y = dot(r3.xyz, float3(0.212500006,0.715399981,0.0720999986));
  r1.xyzw = r2.yyyy * r1.xyzw;
  r3.w = r3.w * r2.x;
  r0.w = 1;
  r0.xyzw = r0.xyzw * r3.xyzw + r1.xyzw;
  r0.xyzw = c025_VisualColorModulator.xyzw * r0.xyzw;
  r0.xyzw = max(float4(0,0,0,0), r0.xyzw);
  r0.w = min(1, r0.w);
  o0.xyzw = r0.xyzw;
  r0.x = -c040_AlphaRefVal * c041_AlphaTestBool + r0.w;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
}