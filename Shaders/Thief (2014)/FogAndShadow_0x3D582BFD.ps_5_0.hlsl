#include "../Includes/Common.hlsl"
#include "../Includes/Oklab.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3); // Scene
Texture2D<float4> t2 : register(t2); // Fog scene
Texture2D<float4> t1 : register(t1); // Half res depth
Texture2D<float4> t0 : register(t0); // Depth

SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[3];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

// Composes scene and fog
// TODO: try to minimize the fog raising blacks?
void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
#if 0 // Disable
  o0 = t3.Sample(s0_s, v0.xy);
  return;
#endif
  float4 r0,r1,r2,r3,r4;
  r0.xy = v0.xy * cb0[1].xy + float2(-0.5,-0.5);
  r0.zw = round(r0.xy);
  r0.xy = r0.xy + -r0.zw;
  int4 r0i;
  r0i.zw = (float2(0,0) < r0.xy) ? 0xFFFFFFFF : 0;
  r0i.xy = (r0.xy < float2(0,0)) ? 0xFFFFFFFF : 0;
  r0.xy = float2(r0i.xy - r0i.zw);
  r0.zw = r0.xy * cb0[1].zw + v0.xy;
  r1.xy = cb0[1].zw * r0.xy;
  r0.x = t1.Sample(s2_s, r0.zw).x;
  r2.xyzw = t2.Sample(s3_s, r0.zw).xyzw;
  r0.y = 1 + -cb2[2].y;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r3.w = -cb2[2].x / r0.x;
  r1.z = 0;
  r1.xyzw = v0.xyxy + r1.xzzy;
  r0.x = t1.Sample(s2_s, r1.xy).x;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r3.y = -cb2[2].x / r0.x;
  r0.x = t1.Sample(s2_s, r1.zw).x;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r3.z = -cb2[2].x / r0.x;
  r0.x = t1.Sample(s2_s, v0.xy).x;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r3.x = -cb2[2].x / r0.x;
  float depth = t0.SampleLevel(s1_s, v0.xy, 0).x;
  r0.x = depth;
  r0.x = r0.x + -r0.y;
  r0.x = min(-9.99999996e-013, r0.x);
  r0.x = -cb2[2].x / r0.x;
  r3.xyzw = r3.xyzw + -r0.xxxx;
  r0.x = 0.05 * r0.x;
  r0.xyzw = (abs(r3.xyzw) < r0.xxxx);
  r0.xyzw = r0.xyzw ? float4(0.5625,0.1875,0.1875,0.0625) : float4(9.99999975e-005,9.99999975e-005,9.99999975e-005,9.99999975e-005);
  r3.x = r0.x + r0.y;
  r3.x = r3.x + r0.z;
  r3.x = r3.x + r0.w;
  r0.xyzw = r0.xyzw / r3.xxxx;
  r3.xyzw = t2.Sample(s3_s, r1.xy).xyzw;
  r1.xyzw = t2.Sample(s3_s, r1.zw).xyzw;
  r3.xyzw = r3.xyzw * r0.yyyy;
  r4.xyzw = t2.Sample(s3_s, v0.xy).xyzw;
  r3.xyzw = r0.xxxx * r4.xyzw + r3.xyzw;
  r1.xyzw = r0.zzzz * r1.xyzw + r3.xyzw;
  float4 fogColor = r0.wwww * r2.xyzw + r1.xyzw;
  float3 sceneColor = t3.Sample(s0_s, v0.xy).xyz;
  float3 fogAndScene = fogColor.rgb + sceneColor * fogColor.a;
#if 0 // Luma: saturate background to retain vanilla output (on alpha) (i'm not sure what's the consequence)
  float3 fogAndSatScene = fogColor.rgb + saturate(sceneColor) * fogColor.a;
  float3 fogAndSceneDiff = saturate(sceneColor) - (fogAndSatScene);
#else
  float3 fogAndSceneDiff = sceneColor - fogAndScene;
#endif
  o0.w = 1 - dot(abs(fogAndSceneDiff), float3(0.333332986,0.333332986,0.333332986));
  r1.xy = cb0[2].xy * v0.xy;
  r1.xyz = t4.Sample(s4_s, r1.xy).xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);

#if 1 // TODO: expose define and DVS2 setting below
  {
    float3 backgroundColor = sceneColor * fogColor.a;
    float3 sceneWithFog = fogAndScene;
    float3 prevSceneWithFog = sceneWithFog.rgb;
    float3 backgroundUCS = LINEAR_TO_UCS(backgroundColor.rgb, CS_DEFAULT);
    float3 sceneWithFogUCS = LINEAR_TO_UCS(sceneWithFog.rgb, CS_DEFAULT);
    //float3 fogUCS = LINEAR_TO_UCS(additiveFog.rgb, CS_DEFAULT);

    // Start from the non fogged scene background and restore some of the fogged scene brightness in the distance (not close to the camera, to avoid raised blacks)
    backgroundUCS.x = lerp(backgroundUCS.x, sceneWithFogUCS.x, pow(saturate(depth), 33.333)); // Heuristically found value (hopefully the depth far plane is consistent through the game)
    float3 backgroundColorWithFogBrightness = UCS_TO_LINEAR(backgroundUCS, CS_DEFAULT);
    
    // Restore the fog hue and chrominance, to indeed have it look similar to vanilla
    const float fogSaturation = 1.0; // Values beyond 0.7 and 0.9 make the fog look a bit closer to vanilla, without raising blacks, but it looks nicer with extra saturation and goes into BT.2020
    sceneWithFog.rgb = RestoreHueAndChrominance(backgroundColorWithFogBrightness, sceneWithFog.rgb, 1.0, fogSaturation); // I'm a bit confused as to why but if we restore any less hue than 1, it looks either broken or bad

    fogAndScene = lerp(prevSceneWithFog, sceneWithFog.rgb, DVS2);
  }
#endif

  r0.xyz = r1.xyz * float3(5.99999985e-005,5.99999985e-005,5.99999985e-005) + fogAndScene;
  r1.xyzw = t5.Sample(s5_s, v0.xy).xyzw;
  r1.xyz = cb0[2].zzz * r1.xyz;
  r1.xyz = r1.www * -r1.xyz + r1.xyz;
  o0.xyz = r1.xyz + r0.xyz;

  // Luma: UNORM emulation:
  o0.w = saturate(o0.w);
}