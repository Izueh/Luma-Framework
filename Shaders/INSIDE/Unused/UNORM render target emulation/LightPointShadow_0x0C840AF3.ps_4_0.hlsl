Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

cbuffer cb0 : register(b0)
{
  float4 cb0[8];
}

#define cmp

// Seemengly purely a shadow
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  nointerpolation float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyz = v1.xyz / v0.w;
  int4 r1i = int4(v0.xy, 0, 0);
  r2.xyzw = t0.Load(r1i.xyw).xyzw;
  r1.xyzw = t1.Load(r1i.xyz).xyzw;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = cb0[7].z * r2.x + cb0[7].w;
  r0.w = 1 / r0.w;
  r0.xyz = r0.w * r0.xyz + v2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r0.xyz);
  r0.y = r0.w * r0.w;
  r0.y = r0.w * r0.y;
  r0.z = r0.y * v2.w + 1;
  r0.z = cmp(r0.z < 0);
  if (r0.z != 0) discard;
  r0.z = rsqrt(r0.y);
  r0.y = v2.w * r0.y;
  r0.y = r0.y * v1.w + v1.w;
  r0.x = r0.x * r0.z;
  r0.z = cmp(r0.x < 0);
  if (r0.z != 0) discard;
  r0.z = r0.x * r0.y;
  r0.x = r0.x * r0.y + 0.5;
  r0.x = r0.z / r0.x;
  r0.yz = v0.xy + v0.zz;
  r0.yz = cb0[1].xx + r0.yz;
  r0.yz = float2(5.39870024,5.44210005) * r0.yz;
  r0.yz = frac(r0.yz);
  r1.xy = float2(21.5351009,14.3136997) + r0.yz;
  r0.w = dot(r0.zy, r1.xy);
  r0.yz = r0.yz + r0.w;
  r0.y = r0.y * r0.z;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.y;
  r0.yzw = float3(75.0490875,75.0495682,75.0496063) * r0.y;
  r0.yzw = frac(r0.yzw);
  r1.xyz = frac(r1.xyz);
  r0.yzw = r1.xyz + r0.yzw;
  o0.xyz = r0.yzw * -0.1 + r0.x;
  o0.w = 1;

  // Luma: prevents broken lighting given that UNORM RT blending pre-clamped to 0-1 before blending.
  // RGB here was ignored in the blend formula (zeroed), so it shouldn't matter but we clamp it for extra safety,
  // while A was used as a background darkening factor, hence we wouldn't want it to go beyond 0-1.
  o0 = max(o0, 0.0);
}