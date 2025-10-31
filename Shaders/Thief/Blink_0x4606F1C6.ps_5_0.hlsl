Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[26];
}

void main(
  float4 v0 : COLOR0,
  float4 v1 : TEXCOORD2,
  float4 v2 : TEXCOORD8,
  float2 v3 : TEXCOORD9,
  uint v4 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1;
  r0.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r0.xy = r0.xy * cb0[24].xy + cb0[24].zw;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = min(1, r0.x);
  r0.x = r0.x * r0.x;
  r0.x = saturate(cb0[25].x * r0.x + cb0[25].y);
  r0.yz = v3.xy / v2.ww;
  r0.yz = r0.yz * cb2[0].xy + cb2[0].wz;
  r0.yzw = t0.Sample(s0_s, r0.yz).xyz;
  r1.xyz = cb0[23].xyz + -r0.yzw;
  r0.xyz = r0.xxx * r1.xyz + r0.yzw;
  o0.xyz = cb0[22].xyz + r0.xyz;
  o0.w = 1;
  o1.xyzw = float4(0,0,0,0);
  o2.xyzw = float4(0,0,0,1);
  o3.xyzw = float4(0,0,0,0);
}