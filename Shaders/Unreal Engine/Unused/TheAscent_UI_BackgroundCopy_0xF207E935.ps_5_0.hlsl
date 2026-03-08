Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[67];
}

void main(
  linear noperspective float2 v0 : TEXCOORD0,
  float4 v1 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = -cb0[66].xy + v0.xy;
  r0.xy = max(cb0[65].xy, r0.xy);
  r0.xy = min(cb0[65].zw, r0.xy);
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r1.xyzw = cb0[66].xyxy * float4(1,-1,-1,1) + v0.xyxy;
  r1.xyzw = max(cb0[65].xyxy, r1.xyzw);
  r1.xyzw = min(cb0[65].zwzw, r1.xyzw);
  r2.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.xyzw = t0.Sample(s0_s, r1.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  r1.xy = cb0[66].xy + v0.xy;
  r1.xy = max(cb0[65].xy, r1.xy);
  r1.xy = min(cb0[65].zw, r1.xy);
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  o0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
}