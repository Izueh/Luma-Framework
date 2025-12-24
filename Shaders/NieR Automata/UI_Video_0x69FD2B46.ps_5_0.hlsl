// ---- Created with 3Dmigoto v1.3.16 on Tue Dec 16 06:35:04 2025



// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(-0.00392156886,0.0549999997,0.0549999997,0.0549999997) + v1.wxyz;
  r0.x = cmp(r0.x < 0);
  r0.yzw = float3(0.947867334,0.947867334,0.947867334) * r0.yzw;
  r0.yzw = log2(abs(r0.yzw));
  r0.yzw = float3(2.4000001,2.4000001,2.4000001) * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  if (r0.x != 0) discard;
  o0.xyzw = v1.xyzw;
  r1.xyz = cmp(float3(0.0392800011,0.0392800011,0.0392800011) >= v1.xyz);
  r2.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * v1.xyz;
  o1.xyz = r1.xyz ? r2.xyz : r0.yzw;
  o1.w = v1.w;
  return;
}