cbuffer _Globals : register(b0)
{
  row_major float4x4 g_SMapTM[4] : packoffset(c101);
  float4 g_DbgColor : packoffset(c117);
  float4 g_FilterTaps[8] : packoffset(c118);
  float4 g_FadingParams : packoffset(c126);
  float4 g_CSMRangesSqr : packoffset(c127);
  float2 g_SMapSize : packoffset(c128);
  float4 g_CameraOrigin : packoffset(c129);
  float4 Params : packoffset(c0);
  float4 LensParams : packoffset(c1);
}

SamplerState TMU0_Sampler_sampler_s : register(s0);
Texture2D<float4> TMU0_Sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = float2(-0.5,-0.5) + v1.xy;
  r0.xy = LensParams.yz * r0.xy;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = saturate(r0.x / LensParams.x);
  r0.yz = Params.xy * r0.xx + v1.xy;
  r1.x = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, r0.yz).x;
  r0.yz = -Params.xy * r0.xx + v1.xy;
  r1.z = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, r0.yz).z;
  r2.xyzw = TMU0_Sampler.Sample(TMU0_Sampler_sampler_s, v1.xy).xyzw;
  r1.yw = r2.yw;
  r1.xyzw = -r2.xyzw + r1.xyzw;
  r0.xyzw = r1.xyzw * r0.x;
  o0.xyzw = Params.z * r0.xyzw + r2.xyzw;
}