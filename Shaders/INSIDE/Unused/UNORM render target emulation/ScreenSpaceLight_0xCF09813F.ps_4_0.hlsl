Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

// These have no depth test
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = float2(5.39870024,5.44210005) * v1.zw;
  r0.xy = frac(r0.xy);
  r0.zw = float2(21.5351009,14.3136997) + r0.xy;
  r0.z = dot(r0.yx, r0.zw);
  r0.xy = r0.xy + r0.zz;
  r0.x = r0.x * r0.y;
  r1.xyzw = float4(95.4307022,97.5901031,93.8368988,91.6931) * r0.xxxx;
  r0.xyzw = float4(75.0490875,75.0495682,75.0496063,75.0496674) * r0.xxxx;
  r0.xyzw = frac(r0.xyzw);
  r1.xyzw = frac(r1.xyzw);
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyzw = float4(-0.5,-0.5,-0.5,-0.5) + r0.xyzw;
  r0.xyzw = float4(0.00392156886,0.00392156886,0.00392156886,0.03125) * r0.xyzw;
  r1.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  o0.xyzw = v2.xyzw * r1.w + -r0.xyzw;
  
  // Luma: typical UNORM like clamping
  o0.rgb = max(o0.rgb, 0.0);
  o0.a = saturate(o0.a);
}