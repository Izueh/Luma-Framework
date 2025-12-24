Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[15];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = -cb0[14].xy + v0.xy;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r1.xy = cb0[14].xy + v0.xy;
  r1.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r2.xyzw = cb0[14].xyxy * float4(1,-1,-1,1) + v0.xyxy;
  r3.xyzw = t0.Sample(s0_s, r2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, r2.zw).xyzw;
  r1.xyz = r3.xyz + r1.xyz;
  r1.w = max(r3.w, r1.w);
  r0.xyz = r1.xyz + r0.xyz;
  r0.w = max(r2.w, r0.w);
  r0.xyz = r0.xyz + r2.xyz;
  o0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  o0.w = max(r1.w, r0.w);
}