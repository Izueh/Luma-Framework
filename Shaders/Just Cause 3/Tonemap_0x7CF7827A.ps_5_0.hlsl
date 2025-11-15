#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

cbuffer cbConsts : register(b1)
{
  float4 Consts[17] : packoffset(c0);
}

#if 0
01F41F2D
0BAC4255
0C35F299
148CD952
15BC0ABC
1C087BA1
2319D5A4
2607E7C0
288B16D3
2FB48E77
371AD4D5
3753CA0A
38ABE9E7
3EC5DBB9
4030BF6E
49704266
4A9BFEC5
5040BB59
60EB1F22
6A1C711F
6C0BCB6B
6F6BFEDA
75190444
79193F1D
7CF7827A
7F138E1C
83CC89FB
8610E7F5
87F34BAA
8A824E55
8D59471A
8D8F7072
92550B56
96DA986B
9C62A6F9
9D857B42
9FBCF8A7
A274F081
A2DF6AD4
A91CF149
A91F8AB9
A9CEF67D
ADAFB4CD
BCF2BA69
BF1F1C29
C16B4E6B
CD75CC78
D0F9B11B
D4B1C6E9
DC0FE377
DED46AD7
DF7BF9E8
E1ECF661
F21C9CBA
F4E80E62
F7078237
FA0676EF
FA796E93
FDBDB73F
#endif

SamplerState SceneTexture_s : register(s0); // Clamp linear sampler
SamplerState BlurredSceneTexture_s : register(s1); // Clamp linear sampler
SamplerState DepthTexture_s : register(s2); // Clamp linear sampler
SamplerState BloomTexture_s : register(s3); // Clamp linear sampler
SamplerState SecondaryBloomTexture_s : register(s4); // Clamp linear sampler
SamplerState EdgeFadeTexture_s : register(s9); // Clamp linear sampler
SamplerState LensDirtTexture_s : register(s10); // Wrap linear sampler
SamplerState ColorCorrectionTexture_s : register(s11); // Clamp linear sampler
SamplerState HeatHazeTexture_s : register(s13); // Wrap linear sampler
SamplerState BokehFocusTexture_s : register(s14); // Clamp linear sampler
Texture2D<float4> SceneTexture : register(t0);
Texture2D<float4> BlurredSceneTexture : register(t1);
Texture2D<float> DepthTexture : register(t2);
Texture2D<float4> BloomTexture : register(t3);
Texture2D<float4> SecondaryBloomTexture : register(t4);
Texture2D<float4> EdgeFadeTexture : register(t9);
Texture2D<float4> LensDirtTexture : register(t10);
Texture3D<float4> ColorCorrectionTexture : register(t11); // 32x 3D LUT
Texture2D<float4> HeatHazeTexture : register(t13);
Texture2D<float> BokehFocusTexture : register(t14);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;

  float2 uv = Consts[16].xy * v1.xy;

  float depth = DepthTexture.Sample(DepthTexture_s, uv).x;
  float linearDepth = depth * Consts[2].z + Consts[2].w;
  float inverseLinearDepth = 1.0 / linearDepth;

  r2.xyzw = uv.x * Consts[5].xyzw + Consts[6].xyzw * uv.y + depth * Consts[7].xyzw + Consts[8].xyzw;
  r2.xyz = r2.xzy / r2.w;
  r2.xyz = r2.xyz - Globals[4].xzy;
  r0.z = rsqrt(dot(r2.xyz, r2.xyz));
  r2.xyz = r2.zxy * r0.z;
  if (0 < Consts[15].w) {
    r0.z = min(abs(r2.y), abs(r2.z));
    r0.w = max(abs(r2.y), abs(r2.z));
    r0.w = 1 / r0.w;
    r0.z = r0.z * r0.w;
    r0.w = r0.z * r0.z;
    r1.w = r0.w * 0.0208350997 + -0.0851330012;
    r1.w = r0.w * r1.w + 0.180141002;
    r1.w = r0.w * r1.w + -0.330299497;
    r0.w = r0.w * r1.w + 0.999866009;
    r1.w = r0.z * r0.w;
    r2.w = (abs(r2.z) < abs(r2.y));
    r1.w = r1.w * -2 + 1.57079637;
    r1.w = r2.w ? r1.w : 0;
    r0.z = r0.z * r0.w + r1.w;
    r0.w = (-r2.z < r2.z);
    r0.w = r0.w ? -3.141593 : 0;
    r0.z = r0.z + r0.w;
    r0.w = min(-r2.y, -r2.z);
    r1.w = max(-r2.y, -r2.z);
    r0.w = (r0.w < -r0.w);
    r1.w = (r1.w >= -r1.w);
    r0.w = r0.w ? r1.w : 0;
    r0.z = r0.w ? -r0.z : r0.z;
    r0.z = 3.14159012 + r0.z;
    r3.x = 0.159155071 * r0.z;
    r0.z = abs(r2.x) * 0.5 + 0.5;
    r3.y = -r2.x * r0.z;
    r0.z = saturate(Consts[15].z * inverseLinearDepth);
    r0.w = Consts[15].x * r0.z;
    r1.w = saturate(-r2.x);
    r1.w = r1.w * r1.w;
    r1.w = r1.w * Consts[15].y + 1;
    r1.w = 1 / r1.w;
    r0.w = r1.w * r0.w;
    r3.xyzw = -Consts[14].xyzw + r3.xyxy;
    r3.xyzw = float4(8,3,25,8) * r3.xyzw;
    r2.yz = HeatHazeTexture.Sample(HeatHazeTexture_s, r3.zw).yw;
    r2.yz = float2(-0.5,-0.5) + r2.yz;
    r2.yz = float2(0.75,0.75) * r2.yz;
    r3.xyz = HeatHazeTexture.Sample(HeatHazeTexture_s, r3.xy).xyw;
    r2.w = -0;
    r2.yzw = r3.xyz + r2.wyz;
    r2.yzw = float3(0,-0.5,-0.5) + r2.yzw;
    r3.xy = r2.wz * r0.w;
    r3.xy = r3.xy * r2.y + v1.xy;
    r0.w = dot(r2.zw, r2.zw);
    r0.w = sqrt(r0.w);
    r0.w = r1.w * 0.300000012 + r0.w;
    r0.z = r0.w * r0.z;
    r0.z = r0.z * r1.w;
    r0.z = Consts[15].w * r0.z;
    r1.z = saturate(r0.z * r2.y);
    r1.y = r1.z;
  } else {
    r3.xy = v1.xy;
    r1.y = 0;
  }

  r2.x = saturate(r2.x);
  r0.z = r2.x * r2.x;
  r0.z = r0.z * Consts[13].x + 1;
  r0.w = saturate(inverseLinearDepth * Consts[4].x + -Consts[4].y);
  r0.z = r0.w / r0.z;
  r0.z = saturate(r0.z * Consts[0].w + Consts[0].z);
  r0.z = -0.1 + r0.z;
  r0.z = saturate(16 * r0.z);
  r0.x = BokehFocusTexture.Sample(BokehFocusTexture_s, uv).x;
  r0.x = r0.z + r0.x;
  r0.y = 4 * r1.y;
  r0.y = min(1, r0.y);
  r0.x = saturate(r0.x + r0.y);

  float3 scene = SceneTexture.Sample(SceneTexture_s, uv).xyz;
  float3 blurredScene = BlurredSceneTexture.Sample(BlurredSceneTexture_s, uv).xyz;
  float3 bloomedScene = BloomTexture.Sample(BloomTexture_s, uv).xyz;
  float3 secondaryBloomedScene = SecondaryBloomTexture.Sample(SecondaryBloomTexture_s, uv).xyz;
  float3 lensDirt = LensDirtTexture.Sample(LensDirtTexture_s, r3.xy).xyz;

  // TODO: the auto expose level is determined by the output brightness of this shader, so in HDR it'd be different
  float3 tonemappedColor = lerp(scene, blurredScene, r0.x);
  tonemappedColor = (tonemappedColor * Consts[2].x) + (bloomedScene * Consts[3].x) + secondaryBloomedScene * (lensDirt * Consts[3].z + Consts[3].y);
#if 0 // Test: raw output
  o0.xyz = tonemappedColor;
  o0.w = sqrt(GetLuminance(o0.xyz));
  return;
#endif
  if (Consts[13].z == 0.0) {
    r1.xyz = EdgeFadeTexture.Sample(EdgeFadeTexture_s, v1.xy).xyz;
    r1.xyz = lerp(1.0, r1.xyz, Consts[2].y);
    tonemappedColor *= r1.xyz;
  } else {
    r2.xy = v1.xy * 2.0 - 1.0;
    r2.xy = abs(r2.xy) * 0.5 + 0.5;
    r2.xy = 1.0 - r2.xy;
    r2.xyz = EdgeFadeTexture.Sample(EdgeFadeTexture_s, r2.xy).xyz;
    r2.xyz = lerp(1.0, r2.xyz * r2.xyz, Consts[2].y);
    tonemappedColor *= r2.xyz;
  }

  float midGreyIn = MidGray;
  float midGreyLutIn = 1.0 - exp2(midGreyIn * -Consts[10].x); // Ignore "Consts[1].xyz" as it might be used to do fade to blacks etc
  midGreyLutIn = midGreyLutIn * (1.0 - (1.0 / 32.0)) + (0.5 / 32.0);
  float3 midGreyLutOut3 = sqr(ColorCorrectionTexture.Sample(ColorCorrectionTexture_s, sqrt(saturate(midGreyLutIn))).xyz);
  float midGreyLutOut = average(midGreyLutOut3);
  float3 untonemapped = tonemappedColor * (midGreyLutOut / midGreyIn);

  bool ignore = true;
  if (uv.x > 0.333 || ignore)
  {
    tonemappedColor = Consts[1].xyz * (1.0 - exp2(tonemappedColor * -Consts[10].x)); // "Consts[1].xyz" is usually 1, and "Consts[10].x" is usually 16
  }
  else
  {
    tonemappedColor = untonemapped;
  }
  r0.xyz = tonemappedColor;

#if 1
  // LUT is gamma 2.0 space (weird, not sure they whould have been authored that way)
  r0.xyz = sqrt_mirrored(r0.xyz); // Luma: added mirroring (and remove 0-1 clamping) // TODO: useless without LUT extrapolation
  if (uv.x > 0.666 || ignore)
  {
  r0.xyz = r0.xyz * (1.0 - (1.0 / 32.0)) + (0.5 / 32.0);
  r0.xyz = ColorCorrectionTexture.Sample(ColorCorrectionTexture_s, r0.xyz).xyz;
  }
  r0.xyz = sqr_mirrored(r0.xyz); // Luma: added mirroring (problably not useful if the LUT is UNORM)
#endif

  if (DVS4)
  r0.xyz = RestorePostProcess(untonemapped, saturate(tonemappedColor), saturate(r0.xyz), 0.0, false);
  else if (DVS5)
  r0.xyz = (untonemapped);
  else if (!DVS6)
  {
    r0.xyz = lerp(r0.xyz, max(r0.xyz, RestoreLuminance(r0.xyz, untonemapped * 2.5)), sqr(saturate(GetLuminance(r0.xyz) / MidGray)));
    //r0.xyz = lerp(r0.xyz, RestoreLuminance(r0.xyz, untonemapped), sqr(saturate(r0.xyz / MidGray)));
    //r0.xyz = RestoreHueAndChrominance(untonemapped, color_tonemapped_graded, 1.0, 0.0, 0.0, FLT_MAX, 0.0, CS_BT709);
  }

  // Likely user brightness levels (defaults to 1 and 0)
  o0.xyz = r0.xyz * Consts[12].x + Consts[12].y; // Luma: removed saturate
  o0.xyz *= DVS1 * 10;
  float tonemappedLuminance = GetLuminance(o0.xyz); // Luma: fixed BT.601 luminance
  o0.w = sqrt(tonemappedLuminance); // TODO: fix approximation
}