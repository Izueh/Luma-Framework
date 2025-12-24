Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[23];
}

// TODO: this is actually screen space fog?
void main(
  float4 v0 : TEXCOORD4,
  float v1 : TEXCOORD1,
  float2 w1 : TEXCOORD9,
  float4 v2 : TEXCOORD2,
  float3 v3 : TEXCOORD6,
  float4 v4 : TEXCOORD7,
  float4 v5 : TEXCOORD10,
  float4 v6 : TEXCOORD8,
  uint v7 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = w1.xy / v6.ww;
  r0.xy = r0.xy * cb2[0].xy + cb2[0].wz;
  r1.xyz = t1.Sample(s1_s, r0.xy).xyz;
  r0.xyzw = t2.Sample(s2_s, r0.xy).xyzw;
  r0.x = dot(v5.xyzw, r0.xyzw);
  r0.y = dot(v4.xyz, r1.xyz);
  r0.x = r0.y + r0.x;
  r0.y = 1 + r0.x;
  r0.x = r0.x / r0.y;
  r0.x = 1 + -r0.x;
  r0.y = t0.Sample(s0_s, v2.xy).w;
  r0.y = r0.y * r0.y;
  r1.xyz = v0.www * v0.xyz;
  r0.yzw = r0.yyy * r1.xyz + cb0[22].xyz;
  o0.xyz = r0.yzw * r0.xxx;
  o0.w = 0;
}