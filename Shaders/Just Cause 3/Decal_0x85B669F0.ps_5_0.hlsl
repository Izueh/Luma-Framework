cbuffer cbInstanceConsts : register(b1)
{
  float4 InstanceConsts[13] : packoffset(c0);
}

SamplerState DepthMap_s : register(s0);
SamplerState DiffuseAlpha_s : register(s1);
SamplerState Tint_AlphaMask_s : register(s4);
Texture2D<float4> DepthMap : register(t0);
Texture2D<float4> DiffuseAlpha : register(t1);
Texture2D<float4> Tint_AlphaMask : register(t4);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3)
{
  float4 r0,r1,r2;
  r0.xy = v1.xy / v1.ww;
  r1.xyzw = InstanceConsts[1].xyzw * r0.yyyy;
  r1.xyzw = r0.xxxx * InstanceConsts[0].xyzw + r1.xyzw;
  r0.xy = InstanceConsts[12].xy * r0.xy;
  r0.x = DepthMap.Sample(DepthMap_s, r0.xy).x;
  r0.xyzw = r0.xxxx * InstanceConsts[2].xyzw + r1.xyzw;
  r0.xyzw = InstanceConsts[3].xyzw + r0.xyzw;
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = -InstanceConsts[4].xyz + r0.xyz;
  r1.xyz = InstanceConsts[5].yyy * v2.xyz;
  r1.x = dot(r0.xyz, r1.xyz);
  r2.xyz = InstanceConsts[5].xxx * v3.xyz;
  r1.y = dot(r0.xyz, r2.xyz);
  r2.xyz = InstanceConsts[5].zzz * v4.xyz;
  r1.z = dot(r0.xyz, r2.xyz);
  r0.xyz = r1.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  r0.w = min(1, abs(r1.z));
  r0.w = InstanceConsts[5].w * -r0.w + 1;
  r1.xyz = cmp(r0.xyz < float3(0,0,0));
  r1.x = asfloat(asint(r1.y) | asint(r1.x));
  r1.x = asfloat(asint(r1.z) | asint(r1.x));
  if (r1.x != 0) discard;
  r1.xyz = float3(1,1,1) + -r0.xyz;
  r1.xyz = cmp(r1.xyz < float3(0,0,0));
  r0.z = asfloat(asint(r1.y) | asint(r1.x));
  r0.z = asfloat(asint(r1.z) | asint(r0.z));
  if (r0.z != 0) discard;
  r1.xy = r0.xy * InstanceConsts[6].xy + InstanceConsts[6].zw;
  r0.xy = r0.xy * InstanceConsts[10].xy + InstanceConsts[10].zw;
  r2.xyzw = Tint_AlphaMask.Sample(Tint_AlphaMask_s, r0.xy).xyzw;
  r0.x = DiffuseAlpha.Sample(DiffuseAlpha_s, r1.xy).x;
  r0.x = -InstanceConsts[7].z + r0.x;
  r0.y = InstanceConsts[7].w + -InstanceConsts[7].z;
  r0.y = 1 / r0.y; // This one is fine
  r0.x = saturate(r0.x * r0.y);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.y = r2.w + -r2.y;
  r0.y = InstanceConsts[8].w * r0.y + r2.y;
  r1.xyz = InstanceConsts[8].xyz * r2.xyz + -InstanceConsts[8].xyz;
  r0.y = 1 + -r0.y;
  r0.y = -InstanceConsts[9].w * r0.y + 1;
  r0.x = r0.x * r0.y;
  r0.y = saturate(InstanceConsts[4].w);
  r0.x = r0.x * r0.y;
  r0.x = r0.w * r0.x;
  r0.yzw = InstanceConsts[9].xyz * r0.xxx;
  o3.w = r0.x;
  o0.w = saturate(r0.y);
  r0.x = InstanceConsts[9].w * InstanceConsts[8].w;
  o0.xyz = r0.xxx * r1.xyz + InstanceConsts[8].xyz;
  o1.w = saturate(r0.z);
  o2.w = saturate(r0.w);
  r0.x = dot(v4.xyz, v4.xyz);
  r0.x = abs(r0.x) > 0.001 ? rsqrt(r0.x) : 0;
  r0.xyz = v4.xyz * r0.xxx;
  o1.xyz = r0.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  o2.xyz = saturate(InstanceConsts[11].xyz); // Luma: fix decals (helipad H) having huge values from cbuffers that aren't clamped with float RTs // TODO: clamp all materials with shader patching...? We did it now (currently disabled)!!!
  o3.x = InstanceConsts[11].w;
  o3.yz = float2(0,0);
}