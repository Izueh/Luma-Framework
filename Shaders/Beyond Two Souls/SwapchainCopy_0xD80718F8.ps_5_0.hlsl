Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[8];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  r0.xyz = t0.Sample(s0_s, v1.xy).xyz;
  
  // Apply user gamma
#if 1 // Luma: make pow safe
  r0.xyz = pow(abs(r0.xyz), cb0[7].xyz) * sign(r0.xyz);
#else
  r0.xyz = pow(r0.xyz, cb0[7].xyz);
#endif

  o0.xyz = r0.xyz;
  o0.w = 0;
}