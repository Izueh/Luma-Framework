cbuffer cbConsts : register(b1)
{
  float4 Consts : packoffset(c0);
}

void main(
  out float4 o0 : SV_Target0)
{
  o0.xyzw = Consts.xyzw;
}