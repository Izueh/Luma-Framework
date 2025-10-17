Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[1];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[7];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

void main(
  float4 v0 : SV_POSITION0,
  float v1 : TEXCOORD0,
  float3 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = float2(-1,-1) + cb1[6].zw;
  r0.zw = v0.xy * r0.xy + cb0[8].zz;
  r0.xy = v0.xy * r0.xy;
  r1.xyzw = t0.SampleLevel(s0_s, r0.xy, 0).xyzw;
#if 0 // Prevent negative shadow... This has been moved to the "Luma_SanitizeLighting" shader as a unified pass.
  r1.xyz = min(1.0, r1.xyz);
#endif
  r1.xyz = max(float3(0.000977517106,0.000977517106,0.000977517106), r1.xyz);
  r1.xyz = log2(r1.xyz); // log2(1)->0, log2(0)->-INF
  r1.xyz = cb0[7].xyz - r1.xyz; // The cbuffer var would usually be zero or one
  r0.xy = float2(5.39870024,5.44210005) * r0.zw;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r0.yzw = float3(95.4307022,97.5901031,93.8368988) * r0.x;
  r2.xyz = float3(75.0490875,75.0495682,75.0496063) * r0.x;
  r0.xyz = frac(r0.yzw) + frac(r2.xyz);
  r0.xyz -= 0.5;

  r0.w = max(0, v1.x);
  r0.w = min(cb2[0].w, r0.w);
  r1.xyz = lerp(r1.xyz * w1.xyz, cb2[0].xyz, r0.w); // Vertex color * lighting color

  o0.xyz = r0.xyz * float3(0.00392156886,0.00392156886,0.00392156886) + r1.xyz; // Add dithering
  o0.w = 1;
#if 0 // Don't allow negative drawing (mostly caused by dithering). Not needed until proven otherwise
  o0.rgb = max(o0.rgb, 0.0);
#endif
}