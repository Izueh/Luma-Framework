cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = v3.y / v3.z;
  r0.x = saturate(cb0[10].y + abs(r0.x));
  r0.y = saturate(cb0[10].y + v3.y);
  r0.x = r0.y + -r0.x;
  r0.x = cb0[11].w * r0.x;
  r0.y = max(0, v3.x);
  r0.y = min(cb2[0].w, r0.y);
  r0.z = cb0[10].z * r0.y;
  r1.xyz = cb2[0].xyz + -cb0[11].xyz;
  r1.xyz = r0.zzz * r1.xyz + cb0[11].xyz;
  r2.xyz = cb2[0].xyz + -v1.xyz;
  r0.yzw = r0.yyy * r2.xyz + v1.xyz;
  r1.xyz = r1.xyz + -r0.yzw;
  r0.xyz = r0.xxx * r1.xyz + r0.yzw;
  r1.xy = cb1[1].xx + v2.xy;
  r1.xy = float2(5.39870024,5.44210005) * r1.xy;
  r1.xy = frac(r1.xy);
  r1.zw = float2(21.5351009,14.3136997) + r1.xy;
  r0.w = dot(r1.yx, r1.zw);
  r1.xy = r1.xy + r0.ww;
  r0.w = r1.x * r1.y;
  r1.xyz = float3(95.4307022,97.5901031,93.8368988) * r0.www;
  r2.xyz = float3(75.0490875,75.0495682,75.0496063) * r0.www;
  r2.xyz = frac(r2.xyz);
  r1.xyz = frac(r1.xyz);
  r1.xyz = r1.xyz + r2.xyz;
  r1.xyz = float3(-0.5,-0.5,-0.5) + r1.xyz;
  o0.xyz = r1.xyz * float3(0.00392156886,0.00392156886,0.00392156886) + r0.xyz;
  o0.w = v3.w;
  
  // NOTE: this is probably not needed as it's for opaque geometry and the code almost never outputs beyond 0-1 (and when it does it's mostly fine)
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}