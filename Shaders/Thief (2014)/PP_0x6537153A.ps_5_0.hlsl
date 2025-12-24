cbuffer cb0 : register(b0)
{
  float4 cb0[12];
}

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = cb0[10].w + -cb0[10].z;
  r0.x = 1 / r0.x;
  r0.yz = v0.yx * cb0[11].zy + -cb0[10].yx;
  r0.w = dot(r0.yz, r0.yz);
  r0.w = sqrt(r0.w);
  r1.x = -cb0[10].z + r0.w;
  r0.yz = r0.yz / r0.ww;
  r2.xy = float2(-1,-0) + r0.yz;
  r0.x = saturate(r1.x * r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r2.w = r0.y * r0.x;
  r2.z = -1 + cb0[11].w;
  o0.xyzw = cb0[11].x * r2.xyzw + float4(1,0,1,0);
}