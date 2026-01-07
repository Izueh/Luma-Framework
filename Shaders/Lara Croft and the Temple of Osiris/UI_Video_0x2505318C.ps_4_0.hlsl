#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 consta : packoffset(c0);
  float4 crc : packoffset(c1);
  float4 cbc : packoffset(c2);
  float4 adj : packoffset(c3);
  float4 yscale : packoffset(c4);
}

SamplerState samp0_s : register(s0);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
Texture2D<float4> tex2 : register(t2);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = tex2.Sample(samp0_s, v1.zw).xyzw;
  r0.xyz = cbc.xyz * r0.www;
  r1.xyzw = tex1.Sample(samp0_s, v1.zw).xyzw;
  r0.xyz = crc.xyz * r1.www + r0.xyz;
  r0.xyz = adj.xyz + r0.xyz;
  r1.xyzw = tex0.Sample(samp0_s, v1.xy).xyzw;
  r0.xyz = r1.www * yscale.xyz + r0.xyz; // Luma: not sure what matrix they used to decode the videos, possibly BT.601, which would break reds
  r1.xyz = r0.xyz * consta.xyz + float3(0.0549999997,0.0549999997,0.0549999997);
  r0.xyz = consta.xyz * r0.xyz;
  r1.xyz = float3(0.947867274,0.947867274,0.947867274) * r1.xyz;
  r1.xyz = pow(abs(r1.xyz), 2.4) * sign(r1.xyz); // Luma: made safe
  r2.xyz = (float3(0.0404499993,0.0404499993,0.0404499993) >= r0.xyz);
  r0.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
  o0.xyz = r2.xyz ? r0.xyz : r1.xyz;
  o0.w = consta.w;

  // Luma: add a light AutoHDR pass on videos
  if (LumaSettings.DisplayMode == 1)
  {
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.UIPaperWhiteNits);
  }
}