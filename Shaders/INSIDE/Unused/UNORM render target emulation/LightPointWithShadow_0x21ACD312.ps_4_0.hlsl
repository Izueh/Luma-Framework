Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[13];
}

// Seemengly draws both an additive light and a background darkening shadow
void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  nointerpolation float3 v2 : TEXCOORD1,
  nointerpolation float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyz = v1.xyz / v0.www;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r2.xyzw = t0.Load(r1.xyw).xyzw;
  r1.xyzw = t1.Load(r1.xyz).xyzw;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-0.5,-1);
  r0.w = cb1[7].z * r2.x + cb1[7].w;
  r0.w = 1 / r0.w;
  r0.xyz = r0.www * r0.xyz + v2.xyz;
  r0.w = r0.w * v3.x + v3.y;
  r0.w = max(0, r0.w);
  r0.w = min(cb2[0].w, r0.w);
  r0.w = 0.8 * r0.w;
  r2.xyz = cb2[0].xyz * r0.www;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r0.xyz);
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = r0.y * r0.w;
  r0.z = 1.0 - r0.w;
  r0.w = sqrt(r0.w);
  r0.w = 1 - r0.w;
  r0.w = max(0, r0.w);
  if (r0.z < 0) discard;
  r0.z = 0.5 * r0.y;
  int r0i = asint(r0.y) >> 1;
  r0i = 0x5f375a86 - r0i;
  r0.y = asfloat(r0i);
  r1.x = r0.y * r0.y;
  r0.z = -r0.z * r1.x + 1.5;
  r0.yw = r0.yw * r0.zw;
  r0.x = r0.x * r0.y;
  r0.x = r0.x * 0.5 + 0.5;
  r0.x = r0.w * r0.x;
  r0.w = dot(r0.xx, cb0[12].xx);
  r0.xyz = r2.xyz * r0.w; // Pre-multiply alpha
  r1.xy = v0.xy * cb1[6].zw + v0.z;
  r1.xy = cb1[1].xx + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.z;
  r1.x = r1.x * r1.y;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.x;
  r1.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r1.x;
  r1.xyzw = frac(r1.xyzw);
  r2.xyzw = frac(r2.xyzw);
  r1.xyzw = r2.xyzw + r1.xyzw;
  o0.xyzw = r0.xyzw - r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.0666666701);
  
  // Luma: prevents broken lighting given that UNORM RT blending pre-clamped to 0-1 before blending.
  // RGB here was drawing additive lighting, hence we clamp it to >= 0,
  // while A was used as a background darkening factor, hence we wouldn't want it to go beyond 0-1.
  o0.a = saturate(o0.a);
  o0.rgb = max(o0.rgb, 0.0);
}