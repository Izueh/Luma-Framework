cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  // Apply some gamma
#if 1 // Luma: make pow safe
  float3 r0 = pow(abs(v1.xyz), cb0[0].xyz) * sign(v1.xyz);
#else
  float3 r0 = pow(v1.xyz, cb0[0].xyz);
#endif
  o0.xyz = r0;
  o0.w = v1.w;
}