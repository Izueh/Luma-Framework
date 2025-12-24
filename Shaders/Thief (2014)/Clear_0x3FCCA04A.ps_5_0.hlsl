cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}

void main(
  out float4 o0 : SV_Target0)
{
  o0.xyzw = cb0[6].xyzw;
}