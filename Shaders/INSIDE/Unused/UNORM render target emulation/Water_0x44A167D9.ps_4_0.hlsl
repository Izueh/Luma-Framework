Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[11];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  nointerpolation float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.xy = float2(5.39870024,5.44210005) * cb0[10].yy;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.x = 95.4307022 * r0.x;
  r0.x = frac(r0.x);
  r0.xy = v0.xy * cb0[8].xy + r0.xx;
  r1.xyzw = t3.Load(int3(v0.xy, 0)).xyzw;
  r0.z = cb1[7].z * r1.x + cb1[7].w;
  r0.z = 1 / r0.z;
  r1.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r1.xy = float2(-0.5,-0.5) + r1.zx;
  r2.xyzw = t0.Sample(s0_s, v1.zw).xyzw;
  r1.zw = float2(0.5,0.5) + -r2.zx;
  r1.zw = r1.zw + -r1.xy;
  r1.xy = v2.ww * r1.zw + r1.xy;
  r1.xy = v3.xy * r1.xy;
  r2.xyzw = t1.SampleLevel(s1_s, r0.xy, 0).xyzw;
  r0.x = -v2.x + r0.z;
  r0.x = saturate(r0.x / cb0[7].z);
  r0.y = r2.w * 0.5 + 0.5;
  r0.y = r0.x * r0.y;
  r0.z = saturate(-cb1[5].y + v2.x);
  r0.x = r0.x * r0.z;
  r0.x = r0.y * r0.x;
  r0.xy = r1.xy * r0.xx + v0.xy;
  r0.z = v2.y / v2.z;

  int3 pixelPos = int3(r0.xy, 0);
  r3.xyzw = t3.Load(pixelPos).xyzw;
  r0.x = cmp(r0.z < r3.x);
  if (r0.x != 0) {
    r0.xyzw = t2.Load(pixelPos).xyzw;
    r1.xyz = float3(-0.5,-0.5,-0.5) + r2.xyz;
    o0.xyz = r1.xyz * float3(0.00392156886,0.00392156886,0.00392156886) + r0.xyz;
    o0.w = 1;
  
    // Luma: typical UNORM like clamping
    o0.rgb = max(o0.rgb, 0.0);
  } else {
    o0.xyzw = float4(0,0,0,0);
  }
}