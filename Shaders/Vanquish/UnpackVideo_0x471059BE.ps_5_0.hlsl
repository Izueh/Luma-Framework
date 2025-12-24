Texture2DArray<uint4> t0 : register(t0);

cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

// TODO: fix aspect ratio and add AutoHDR (not here because this decodes to 16:9 textures 1920x1080 or something) (the Copy shader 0x91A6C437 runs immediately after this so we could inform it to do AutoHDR)
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
  uint packedRGBA = t0.Load(r0i.xyzw).x;
  // ubfe r0.xyz, l(8, 8, 8, 8), l(16, 8, 0, 0), r0.xxxx
  r0u.x = packedRGBA << (32-(8 + 16));
  r0u.x = r0u.x >> (32-8);
  r0u.y = packedRGBA << (32-(8 + 8));
  r0u.y = r0u.y >> (32-8);
  r0u.z = packedRGBA << (32-(8 + 0));
  r0u.z = r0u.z >> (32-8);
  r0u.w = packedRGBA << (32-(8 + 24));
  r0u.w = r0u.w >> (32-8);
  r0.xyzw = r0u.xyzw; // utof
  o0.xyzw = r0.xyzw / 255.0;
}