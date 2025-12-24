Texture2DArray<uint4> t0 : register(t0);

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  int4 r0i;
  uint4 r0u;
  float2 uv = min(cb0[6].zw, v1.xy);
  r0i.xy = uv; // ftoi
  r0i.zw = asint(cb0[6].yx); // Slice
  uint packedRGB = t0.Load(r0i.xyzw).x;
#if 1
  // ubfe r0.xyz, l(8, 8, 8, 8), l(16, 8, 0, 0), r0.xxxx
  r0u.x = packedRGB << (32-(8 + 16));
  r0u.x = r0u.x >> (32-8);
  r0u.y = packedRGB << (32-(8 + 8));
  r0u.y = r0u.y >> (32-8);
  r0u.z = packedRGB << (32-(8 + 0));
  r0u.z = r0u.z >> (32-8);
#else // Might work too?
  r0u.rgb = uint3((packedRGB >> 16) & 0xFF, (packedRGB >> 8) & 0xFF, (packedRGB >> 0) & 0xFF);
#endif
  r0.xyz = r0u.xyz; // utof
  o0.xyz = r0.xyz / 255.0;
  o0.w = 1;
}