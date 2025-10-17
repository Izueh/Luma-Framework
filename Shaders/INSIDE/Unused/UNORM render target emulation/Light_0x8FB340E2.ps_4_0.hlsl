Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

cbuffer cb3 : register(b3)
{
  float4 cb3[1];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[11];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[8];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[13];
}

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  nointerpolation float3 v3 : TEXCOORD2,
  nointerpolation float2 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.x = cb2[8].w;
  r0.y = cb2[9].w;
  r0.z = cb2[10].w;
  r0.xyz = r0.xyz + r0.xyz;
  r1.xyz = v1.xyz / v0.www;
  r3.xyzw = t0.Load(int3(v0.xy, 0)).xyzw;
  r2.xyzw = t1.Load(int3(v0.xy, 0)).xyzw;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = cb1[7].z * r3.x + cb1[7].w;
  r0.w = 1 / r0.w;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r1.xyz = saturate(-cb0[12].yyy + abs(r0.xyz));
  r1.x = dot(r1.xyz, r1.xyz);
  r1.x = sqrt(r1.x);
  r1.y = 1 + -cb0[12].y;
  r1.x = r1.x / r1.y;
  r1.x = 1 + -r1.x;
  r1.yz = cmp(r1.xx < float2(0,1));
  if (r1.y != 0) discard;
  r1.y = dot(r2.xyz, r2.xyz);
  r3.xyz = v2.xyz / v0.www;
  r3.xyz = r0.www * r3.xyz + v3.xyz;
  r0.w = r0.w * v4.x + v4.y;
  r0.w = max(0, r0.w);
  r0.w = min(cb3[0].w, r0.w);
  r4.xyz = cb3[0].xyz * r0.www;
  r0.w = dot(r3.xyz, r3.xyz);
  r1.w = dot(r2.xyz, r3.xyz);
  r0.w = r1.y * r0.w;
  r0.w = rsqrt(r0.w);
  r0.w = r1.w * r0.w;
  r0.w = r0.w * 0.5 + 0.5;
  r0.w = r1.x * r0.w;
  r1.x = cb0[12].y * cb0[12].y;
  r0.xyz = r0.xyz * r0.xyz + -r1.xxx;
  r1.xyw = max(abs(r0.xyz), abs(r0.zxy));
  r1.xyw = float3(1,1,1) / r1.xyw;
  r2.xyz = min(abs(r0.xyz), abs(r0.zxy));
  r1.xyw = r2.xyz * r1.xyw;
  r2.xyz = r1.xyw * r1.xyw;
  r3.xyz = r2.xyz * float3(0.0208350997,0.0208350997,0.0208350997) + float3(-0.0851330012,-0.0851330012,-0.0851330012);
  r3.xyz = r2.xyz * r3.xyz + float3(0.180141002,0.180141002,0.180141002);
  r3.xyz = r2.xyz * r3.xyz + float3(-0.330299497,-0.330299497,-0.330299497);
  r2.xyz = r2.xyz * r3.xyz + float3(0.999866009,0.999866009,0.999866009);
  r3.xyz = r2.xyz * r1.xyw;
  r3.xyz = r3.xyz * float3(-2,-2,-2) + float3(1.57079637,1.57079637,1.57079637);
  r5.xyz = cmp(abs(r0.zxy) < abs(r0.xyz));
  r3.xyz = r5.xyz ? r3.xyz : 0;
  r1.xyw = r1.xyw * r2.xyz + r3.xyz;
  r2.xyz = cmp(r0.zxy < -r0.zxy);
  r2.xyz = r2.xyz ? float3(-3.14159298,-3.14159298,-3.14159298) : 0;
  r1.xyw = r2.xyz + r1.xyw;
  r2.xyz = min(r0.xyz, r0.zxy);
  r0.xyz = max(r0.xyz, r0.zxy);
  r0.xyz = cmp(r0.xyz >= -r0.xyz);
  r2.xyz = cmp(r2.xyz < -r2.xyz);
  r0.xyz = r0.xyz ? r2.xyz : 0;
  r0.xyz = r0.xyz ? -r1.xyw : r1.xyw;
  r1.xyw = float3(0.318309873,0.318309873,0.318309873) * r0.zxy;
  r0.xyz = saturate(-r0.xyz * float3(0.318309873,0.318309873,0.318309873) + float3(0.5,0.5,0.5));
  r1.xyw = saturate(r1.xyw);
  r0.x = dot(r1.xyw, r0.xyz);
  r0.x = r1.z ? r0.x : 1;
  r0.y = 2 + -r0.x;
  r0.x = r0.x * r0.y;
  r0.x = r0.w * r0.x;
  r0.x = cb0[12].x * r0.x;
  r0.w = r0.w * r0.x;
  r1.xyz = r4.xyz * r0.www;
  r0.xyz = float3(0.800000012,0.800000012,0.800000012) * r1.xyz;
  r1.xy = v0.xy * cb1[6].zw + v0.zz;
  r1.xy = cb1[1].xx + r1.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r1.z = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r1.zz;
  r1.x = r1.x * r1.y;
  r2.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r1.xxxx;
  r1.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r1.xxxx;
  r1.xyzw = frac(r1.xyzw);
  r2.xyzw = frac(r2.xyzw);
  r1.xyzw = r2.xyzw + r1.xyzw;
  o0.xyzw = -r1.xyzw * float4(0.00392156886,0.00392156886,0.00392156886,0.0666666701) + r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}