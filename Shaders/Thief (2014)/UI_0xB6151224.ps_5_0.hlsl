Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[7];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = 0;
  r0.y = -cb0[5].x;
  // Note: yes, these loops were done with float checks
  while (true) {
    r0.z = (cb0[5].x < r0.y);
    if (r0.z != 0) break;
    r1.x = cb0[1].x + r0.y;
    r2.x = r0.x;
    r2.y = -cb0[5].y;
    while (true) {
      r0.z = (cb0[5].y < r2.y);
      if (r0.z != 0) break;
      r1.y = cb0[1].y + r2.y;
      r0.zw = r1.xy * cb0[2].xy + v0.xy;
      r0.z = t0.SampleLevel(s1_s, r0.zw, 0).w;
      r2.x = r2.x + r0.z;
      r2.y = 1 + r2.y;
    }
    r0.x = r2.x;
    r0.y = 1 + r0.y;
  }
  r0.x = cb0[5].w * r0.x;
  r0.x = cb0[5].z * r0.x;
  r0.yz = cb0[1].zw * v0.xy;
  r1.xyzw = t1.Sample(s0_s, r0.yz).xyzw;
  r0.xyzw = cb0[6].xyzw * r0.xxxx;
  r2.x = 1 + -r1.w;
  r0.xyzw = r0.xyzw * r2.xxxx + r1.xyzw;
  r0.xyz = cb0[4].xyz * r0.xyz;
  r1.xyzw = cb0[3].xyzw * r0.wwww;
  o0.xyzw = r0.xyzw * cb0[4].wwww + r1.xyzw;

  // Luma: fix UI negative values to emulate UNORM blends
  o0.w = saturate(o0.w);
  o0.xyz = max(o0.xyz, 0.f);
}