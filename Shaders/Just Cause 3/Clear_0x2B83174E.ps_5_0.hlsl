void main(
  float4 v0 : COLOR0,
  float4 v1 : COLOR1,
  out float4 o0 : SV_Target0)
{
  o0.w = v1.w * v0.w;
  o0.xyz = v0.xyz;
}