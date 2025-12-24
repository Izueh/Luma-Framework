SamplerState YTextureSampler_s : register(s0);
SamplerState cRTextureSampler_s : register(s1);
SamplerState cBTextureSampler_s : register(s2);
Texture2D<float4> YTexture : register(t0);
Texture2D<float4> cRTexture : register(t1);
Texture2D<float4> cBTexture : register(t2);

void main(
  float3 v0 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyzw = cRTexture.Sample(cRTextureSampler_s, v0.xy, int2(0, 0)).yxzw;
  r1.xyzw = cBTexture.Sample(cBTextureSampler_s, v0.xy, int2(0, 0)).xyzw;
  r0.z = r1.x;
  r1.xyzw = YTexture.Sample(YTextureSampler_s, v0.xy, int2(0, 0)).xyzw;
  r0.x = r1.x;
  r0.w = 1;
  r1.y = dot(r0.xyzw, float4(1,-0.714139998,-0.344139993,0.531215072)); // TODO: fix?
  r1.x = dot(r0.xyw, float3(1,1.40199995,-0.703749001));
  r1.z = dot(r0.xzw, float3(1,1.77199996,-0.889474511));
  o0.xyz = pow(abs(r1.xyz), 2.2) * sign(r1.xyz); // Luma: fixed negative values doing abs
  o0.w = v0.z;
}