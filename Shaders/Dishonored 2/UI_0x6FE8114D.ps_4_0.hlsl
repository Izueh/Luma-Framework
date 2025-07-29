// ---- Created with 3Dmigoto v1.3.16 on Mon Dec 30 15:48:00 2024
cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = cb0[0].xyzw;
  return;
}