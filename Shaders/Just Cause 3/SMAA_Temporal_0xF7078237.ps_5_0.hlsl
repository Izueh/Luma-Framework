cbuffer GlobalConstants : register(b0)
{
  float4 Globals[95] : packoffset(c0);
}

cbuffer cbConsts : register(b1)
{
  float4 Consts : packoffset(c0);
}

SamplerState SamplerLinear_s : register(s0);
Texture2D<float4> SceneTexture : register(t0); // Post SMAA (pre TAA)
Texture2D<float4> PrevSceneTexture : register(t1); // Post previous SMAA (pre TAA)
Texture2D<float2> VelocityTexture : register(t2);

// TODO: replace with DLSS, especially if there's jitters (or add them)! The game massively needs good TAA...
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  int4 r0i;
  r0i.x = (int)Consts.x;
  if (r0i.x != 0)
  {
    r0i.x = (int)Consts.y;
    r0.yz = VelocityTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xy;
    r1.xyzw = SceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyzw;
    r0.yz = v1.xy + -r0.yz;
    r2.xyzw = PrevSceneTexture.SampleLevel(SamplerLinear_s, r0.yz, 0).xyzw;

    // Undo encoding on luminance (from tonemapper, on w, before SMAA) to turn it back to linear
    r0.y = r1.w * r1.w;
    r0.w = r2.w * r2.w;

    r0.zw = 0.2 * r0.yw;
    r0.y = r0.y * 0.2 + -r0.w;
    r0.y = sqrt(abs(r0.y));
    r0.y = saturate(-r0.y * Consts.z + 1);
    r0.y = 0.5 * r0.y;
    r3.x = dot(Globals[8].zw, Globals[8].zw);
    r3.x = Consts.w * r3.x;
    r0.zw = r0.zw * r0.zw;
    r0.zw = (r3.x < r0.zw);
    r0.z = asfloat(asint(r0.w) | asint(r0.z));
    r0.z = r0.z ? 0 : r0.y;
    r0.x = r0i.x ? r0.z : r0.y;
    o0.xyzw = r0.x * (r2.xyzw - r1.xyzw) + r1.xyzw; // Lerp
  }
  else
  {
    r0.xyzw = SceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyzw;
    r1.xyzw = PrevSceneTexture.SampleLevel(SamplerLinear_s, v1.xy, 0).xyzw;
    o0.xyzw = (r1.xyzw - r0.xyzw) * 0.5 + r0.xyzw;  // Lerp
  }
}