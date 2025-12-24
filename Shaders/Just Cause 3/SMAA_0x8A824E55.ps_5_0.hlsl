#include "Includes/Common.hlsl"

cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

SamplerState SamplerLinear_s : register(s0);
Texture2D<float4> SceneTexture : register(t0);
Texture2D<float4> AreaWeightTexture : register(t1);
Texture2D<float4> VelocityTexture : register(t2);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  // If SR is active (and thus running later), convert to BT.2020 to avoid SR clipping negative scRGB colors! Unfortunately it will assume colors are BT.709, but it's whatever, it will mostly work anyway
  if (LumaSettings.SRType)
  {
    float4 sceneColor = SceneTexture.Load(int3(v0.xy, 0)).rgba;
    sceneColor.rgb = BT709_To_BT2020(sceneColor.rgb);
    sceneColor.rgb = CorrectOutOfRangeColor(sceneColor.rgb, true, false, 1.0, 1.0, 0.0, CS_BT2020); // Desatuate all colors beyond BT.2020, to avoid them getting clipped
    o0 = sceneColor;
    return; // Skip SMAA, it'd damage DLSS
  }

  float4 r0,r1,r2,r3;
  r0.xy = AreaWeightTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xz;
  r1.y = AreaWeightTexture.SampleLevel(SamplerLinear_s, v2.zw, 0).y;
  r1.w = AreaWeightTexture.SampleLevel(SamplerLinear_s, v2.xy, 0).w;
  r1.xz = r0.xy;
  r0.z = dot(r1.xyzw, float4(1,1,1,1));
  if (r0.z < 9.99999975e-006) {
    r2.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyz;
    r0.zw = VelocityTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xy; // TODO: why does the velocity matter?
    r0.z = dot(r0.zw, r0.zw);
    r0.z = sqrt(r0.z);
    r0.z = 5 * r0.z;
    o0.w = sqrt(r0.z);
    o0.xyz = r2.xyz;
  } else {
    r0.zw = cmp(r1.zx < r1.wy);
    r0.xw = r0.zw ? r1.wy : -r0.yx;
    r1.x = cmp(abs(r0.w) < abs(r0.x));
    r0.yz = 0.0;
    r0.xy = r1.x ? r0.xy : r0.zw;
    r1.xyz = SceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyz;
    r0.zw = -sign(r0.xy) * Globals[8].zw + v1.xy;
    r2.xyz = SceneTexture.SampleLevel(SamplerLinear_s, r0.zw, 0).xyz;
    r0.x = max(abs(r0.y), abs(r0.x));
    r3.xy = VelocityTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xy;
    r0.yz = VelocityTexture.SampleLevel(SamplerLinear_s, r0.zw, 0).xy;
    r0.w = dot(r3.xy, r3.xy);
    r1.w = sqrt(r0.w);
    r0.y = dot(r0.yz, r0.yz);
    r2.w = sqrt(r0.y);
    r2.xyzw = r2.xyzw + -r1.xyzw;
    r0.xyzw = r0.x * r2.xyzw + r1.xyzw;
    r0.w = 5 * r0.w;
    o0.w = sqrt(r0.w);
    o0.xyz = r0.xyz;
  }
}