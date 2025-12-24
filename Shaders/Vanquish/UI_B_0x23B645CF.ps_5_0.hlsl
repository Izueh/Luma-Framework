Texture2D<float4> t15 : register(t15);
Texture2D<float4> t14 : register(t14);
Texture2D<float4> t13 : register(t13);

SamplerState s15_s : register(s15);
SamplerState s14_s : register(s14);
SamplerState s13_s : register(s13);

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.xyzw = t14.Sample(s14_s, v6.xy).xyzw;
  r0.xyzw = asfloat(asint(r0.xyzw) & asint(cb3[72].xyzw));
  r0.xyzw = asfloat(asint(r0.xyzw) | asint(cb3[73].xyzw));
  r1.xyzw = t13.Sample(s13_s, v5.xy).xyzw;
  r1.xyzw = asfloat(asint(r1.xyzw) & asint(cb3[70].xyzw));
  r1.xyzw = asfloat(asint(r1.xyzw) | asint(cb3[71].xyzw));
  r2.xyzw = v7.xyzw;
  r2.xyzw = r1.xyzw * r2.xyzw + v8.xyzw;
  r1.xyzw = t15.Sample(s15_s, v6.xy).xyzw;
  r1.xyzw = asfloat(asint(r1.xyzw) & asint(cb3[74].xyzw));
  r1.xyzw = asfloat(asint(r1.xyzw) | asint(cb3[75].xyzw));
  r0.xyz = float3(1,1,1) + -r1.xyz;
  r3.xyz = float3(1,1,1) + -r2.xyz;
  r0.xyz = r0.xyz + r0.xyz;
  r3.xyz = r0.xyz * -r3.xyz + float3(1,1,1);
  r4.w = dot(r1.xx, r2.xx);
  r1.w = 0 + r4.w;
  r0.xyz = float3(-0.5,-0.5,-0.5) + r1.xyz;
  r4.w = r2.w * r0.w;
  r5.x = cmp(r0.x >= 0);
  r4.x = r5.x ? r3.x : r1.w;
  r5.y = dot(r1.yy, r2.yy);
  r1.y = 0 + r5.y;
  r5.w = dot(r1.zz, r2.zz);
  r1.w = 0 + r5.w;
  r5.y = cmp(r0.y >= 0);
  r4.y = r5.y ? r3.y : r1.y;
  r5.z = cmp(r0.z >= 0);
  r4.z = r5.z ? r3.z : r1.w;
  r0.w = r4.w * 255.0 + 0.0001;
  r0.w = cmp(asuint(cb3[8].z) >= (uint)r0.w);
  if (r0.w != 0) discard;
  o0.xyzw = r4.xyzw;
  
  // Luma: emulate UNORM
  o0.w = saturate(o0.w);
  o0.rgb = max(o0.rgb, 0.0); // Note: this can occasionally draw a copy of the scene back on the scene in a subtractive way and clip or something, but it's not during normal gameplay so it doesn't really matter
}