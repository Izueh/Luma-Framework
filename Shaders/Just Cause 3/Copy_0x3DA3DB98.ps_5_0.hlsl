Texture2D<float4> Tex : register(t0);

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = Tex.Load(int3(v0.xy, 0)).xyzw;
}