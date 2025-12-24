Texture2D<float4> t0 : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  o0.xyzw = t0.Load(int3(v1.xy, 0)).xyzw;
}