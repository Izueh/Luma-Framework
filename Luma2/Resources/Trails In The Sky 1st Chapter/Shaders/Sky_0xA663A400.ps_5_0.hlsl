// Trails in the Sky 1st Chapter: Procedural sky and sun disk (0xA663A400).
// Luma: Boosts the isolated sun disk term in HDR mode to increase outdoor dynamic range without lifting sky/scene exposure.

#include "Game/Common.hlsl"

cbuffer cb_scene : register(b0)
{
   row_major float4x4 view_g : packoffset(c0);
   row_major float4x4 viewInv_g : packoffset(c4);
   row_major float4x4 proj_g : packoffset(c8);
   row_major float4x4 projInv_g : packoffset(c12);
   row_major float4x4 viewProj_g : packoffset(c16);
   row_major float4x4 viewProjInv_g : packoffset(c20);
   float2 vpSize_g : packoffset(c24);
   float2 invVPSize_g : packoffset(c24.z);
   float4 _pad0[49] : packoffset(c25);
   row_major float4x4 prevViewProj_g : packoffset(c74);
   float2 jitterDiff_g : packoffset(c78);
}

cbuffer cb_post_sky2 : register(b3)
{
   row_major float4x4 skyViewProjInv_g : packoffset(c0);
}

cbuffer cb_post_sky : register(b6)
{
   float3 incomingLight_g : packoffset(c0);
   float3 scatteringR_g : packoffset(c1);
   float3 scatteringM_g : packoffset(c2);
   float3 extinctionR_g : packoffset(c3);
   float3 extinctionM_g : packoffset(c4);
   float3 densityScaleHeight_g : packoffset(c5);
   float skyHorizonBottomLimit_g : packoffset(c5.w);
   float3 sunDirection_g : packoffset(c6);
   float skyHorizonTopLimit_g : packoffset(c6.w);
   float mieG_g : packoffset(c7);
   float distanceScale_g : packoffset(c7.y);
   float planetRadius_g : packoffset(c7.z);
   float atmosphereHeight_g : packoffset(c7.w);
   float sunIntensity_g : packoffset(c8);
   float skyLutNearOverFarClip_g : packoffset(c8.y);
   float skyLutCameraFarClip_g : packoffset(c8.z);
   float skyBrightness_g : packoffset(c8.w);
}

SamplerState samLinear : register(s0);
Texture3D<float4> skyLUT : register(t0);
Texture3D<float2> skyLUT2 : register(t1);

void main(
   float4 v0 : SV_Position0,
   float4 v1 : TEXCOORD0,
   out float4 o0 : SV_Target0,
   out float2 o4 : SV_TARGET4)
{
   // Mie phase pre-factor: 3 * (1 - g^2) / (2 * (2 + g^2)) / (4 * PI)
   float g2 = mieG_g * mieG_g;
   float mieK = (3.0 * (1.0 - g2)) / (2.0 * (2.0 + g2)) * 0.0795774715;

   // Reconstruct world-space ray from clip-space UV
   float4 clipPos = float4(v1.zw * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
   float3 worldPosH = float3(
      dot(clipPos.xyz, skyViewProjInv_g[0].xyw),
      dot(clipPos.xyz, skyViewProjInv_g[1].xyw),
      dot(clipPos.xyz, skyViewProjInv_g[2].xyw));
   float worldW = dot(clipPos.xyz, skyViewProjInv_g[3].xyw);
   float3 worldDirUnnorm = worldPosH / worldW;
   float3 viewDir = normalize(worldDirUnnorm);

   float cosTheta = dot(viewDir, -sunDirection_g);

   // Mie scattering phase
   float mieDenom = pow(max(1.0 + g2 - 2.0 * mieG_g * cosTheta, 1e-6), 1.5);
   float miePhase = mieK * ((1.0 + cosTheta * cosTheta) / mieDenom);

   // 3D sky LUT lookup
   float3 lutUVW;
   lutUVW.x = sunDirection_g.y * -0.5 + 0.5;
   lutUVW.y = max(abs(viewDir.y), 0.05) * 0.5 + 0.5;
   lutUVW.z = viewInv_g[1].w / atmosphereHeight_g;

   float4 lut0 = skyLUT.SampleLevel(samLinear, lutUVW, 0);
   float2 lut1 = skyLUT2.SampleLevel(samLinear, lutUVW, 0);
   float3 mieScatter = float3(lut0.w, lut1.x, lut1.y);

   // Sky Rayleigh + Mie terms
   float rayleighPhase = (cosTheta * 0.5 + 1.4) * 0.0636619772;
   float3 mieTerm = miePhase * mieScatter * scatteringM_g;
   float3 skyTerm = (rayleighPhase * lut0.xyz * scatteringR_g + mieTerm) * incomingLight_g;

   // Isolated Henyey-Greenstein sun disk (g = 0.98)
   float sunDenom = pow(max(1.9604 - 1.96 * cosTheta, 1e-7), 1.5) * 12.5663706;
   float sunPhase = (0.0004 / sunDenom) * 0.003;
   float3 sunTerm = sunPhase * mieScatter * sunIntensity_g;

   if (LumaSettings.DisplayMode == 1)
   {
      sunTerm *= LumaSettings.GameSettings.sun_intensity_boost;
   }

   o0.xyz = max((skyTerm + sunTerm) * skyBrightness_g, 0.0);
   o0.w = 1.0;

   // Vanilla sky motion vectors (o4.xy)
   float4 curWorld = float4(worldDirUnnorm, 1.0);
   float3 curClip = float3(
      dot(curWorld, viewProj_g[0]),
      dot(curWorld, viewProj_g[1]),
      dot(curWorld, viewProj_g[3]));
   float2 curScreen = ((curClip.xy / curClip.z) * float2(0.5, -0.5) + 0.5) * vpSize_g;

   float3 prevClip = float3(
      dot(curWorld, prevViewProj_g[0]),
      dot(curWorld, prevViewProj_g[1]),
      dot(curWorld, prevViewProj_g[3]));
   float2 prevScreen = ((prevClip.xy / prevClip.z) * float2(0.5, -0.5) + 0.5) * vpSize_g;

   o4 = (prevScreen - curScreen) + jitterDiff_g;
}
