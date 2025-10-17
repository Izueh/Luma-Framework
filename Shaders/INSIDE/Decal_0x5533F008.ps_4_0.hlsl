TextureCube<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[24];
}

// Note: this might not be a decal!
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float3 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float w3 : TEXCOORD4,
  float3 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = t0.Sample(s0_s, v3.xy).xyzw;
  r0.x = v1.w + r0.w;
  r0.y = (r0.x < 0);
  r0.x = saturate(cb0[20].x * r0.x);
  if (r0.y != 0) discard;
  r0.yz = v2.xy / v2.zz;
  r1.xyzw = t1.SampleLevel(s2_s, r0.yz, 0).xyzw;
  r0.yzw = max(float3(0.000977517106,0.000977517106,0.000977517106), r1.xyz);
  r0.yzw = log2(r0.yzw);
  r0.yzw = cb0[23].xyz + -r0.yzw;
  r0.yzw = r0.yzw * cb0[20].yyy + float3(1,1,1);
  r0.yzw = -cb0[20].yyy + r0.yzw;
  r1.xyzw = t2.Sample(s1_s, v4.xyz).xyzw;
  r1.xyz = r1.xyz * r0.xxx;
  o0.w = r0.x;
  r1.xyz = r1.xyz * cb0[22].xyz + cb0[21].xyz;
  r2.xyz = r1.xyz * r0.yzw;
  r0.xyz = -r1.xyz * r0.yzw + cb1[0].xyz;
  r0.w = max(0, w3.x);
  r0.w = min(cb1[0].w, r0.w);
  o0.xyz = r0.www * r0.xyz + r2.xyz;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}