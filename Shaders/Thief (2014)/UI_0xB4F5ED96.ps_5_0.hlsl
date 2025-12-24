cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}

void main(
  float4 v0 : COLOR0,
  float4 v1 : COLOR1,
  out float4 o0 : SV_Target0)
{
  float4 r0;

  r0.xyzw = v0.xyzw * cb0[6].xyzw + cb0[5].xyzw;
  o0.w = v1.w * r0.w;
  o0.xyz = r0.xyz;
  
  // Luma: fix UI negative values to emulate UNORM blends
  o0.w = saturate(o0.w);
  o0.xyz = max(o0.xyz, 0.f);
}