Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// TODO: this film grain is bad looking and you can clearly see the pattern at high frame rates, it should be fixed in the shader that draws it
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD2,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xyzw = t1.Sample(s1_s, v1.xy).xyzw;
  r1.xyz = 1.0 + -r0.xyz;
  r2.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  //r2.xyz = saturate(r2.xyz); // Luma: removed unnecessary saturate that broken HDR (film grain applies anyway, possibly more intensively in HDR ranges)
  o0.w = r2.w;
  r3.xyz = r2.xyz - 0.5;
  r3.xyz = -r3.xyz * 2.0 + 1.0;
  r1.xyz = -r3.xyz * r1.xyz + 1.0;
  r3.xyz = (r2.xyz >= 0.5);
  r2.xyz = r3.xyz ? 0.0 : r2.xyz;
  r3.xyz = r3.xyz ? 1.0 : 0.0;
  r0.xyz = r2.xyz * r0.xyz;
  r0.xyz = r0.xyz + r0.xyz;
  o0.xyz = r3.xyz * r1.xyz + r0.xyz;
}