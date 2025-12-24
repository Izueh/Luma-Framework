// ---- Created with 3Dmigoto v1.3.16 on Tue Dec 16 06:35:04 2025

SamplerState g_Texture0Sampler_s : register(s0);
Texture2D<float4> g_Texture0 : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_Texture0.Sample(g_Texture0Sampler_s, v1.xy).xyzw;
  r0.xyzw = v2.xyzw * r0.xyzw;
  r1.xyz = log2(abs(r0.xyz));
  r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r2.xyz = cmp(float3(0.00313080009,0.00313080009,0.00313080009) >= r0.xyz);
  r3.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
  o1.xyzw = r0.xyzw;
  o0.xyz = r2.xyz ? r3.xyz : r1.xyz;
  o0.w = 0;
  return;
}