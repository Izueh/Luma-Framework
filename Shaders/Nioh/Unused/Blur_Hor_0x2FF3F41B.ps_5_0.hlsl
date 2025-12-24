cbuffer _Globals : register(b0)
{
  float4 g_vBorderParams : packoffset(c0);
  float2 SimulateHDRParams : packoffset(c1);
}

SamplerState smplAnamorphicBloomFinal_s : register(s0);
Texture2D<float4> smplAnamorphicBloomFinal_Tex : register(t0);

#define cmp

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0.199676, 0, 0, 0},
                              { 0.297323, 0, 0, 0},
                              { 0.091848, 0, 0, 0},
                              { 0.010991, 0, 0, 0},
                              { 0.297323, 0, 0, 0},
                              { 0.091848, 0, 0, 0},
                              { 0.010991, 0, 0, 0} };
  float4 r0,r1,r2,r3;
  float4 v[3] = { v2,v3,v4 };
  r0.xyzw = float4(0,0,0,0);
  int4 r1i = 0;
  while (true) {
    if (r1i.x >= 3) break;
    r1i.y = r1i.x << 1;
    r2.xyzw = smplAnamorphicBloomFinal_Tex.Sample(smplAnamorphicBloomFinal_s, v[r1i.x].xy).xyzw;
    r3.xyzw = v[r1i.x].xyzw + float4(-0.5,-0.5,-0.5,-0.5);
    r3.xyzw = saturate(abs(r3.xyzw) * g_vBorderParams.xyxy + g_vBorderParams.zwzw);
    r1.zw = r3.xz * r3.yw;
    r2.xyzw = r2.xyzw * r1.zzzz;
    r2.xyzw = icb[r1i.y+0].xxxx * r2.xyzw + r0.xyzw;
    r1i.y = mad(r1i.x, 2, 1);
    r3.xyzw = smplAnamorphicBloomFinal_Tex.Sample(smplAnamorphicBloomFinal_s, v[r1i.x].zw).xyzw;
    r3.xyzw = r3.xyzw * r1.wwww;
    r0.xyzw = icb[r1i.y+0].xxxx * r3.xyzw + r2.xyzw;
    r1i.x++;
  }
  r1.xyzw = smplAnamorphicBloomFinal_Tex.Sample(smplAnamorphicBloomFinal_s, v1.xy).xyzw;
  r2.xy = float2(-0.5,-0.5) + v1.xy;
  r2.xy = saturate(abs(r2.xy) * g_vBorderParams.xy + g_vBorderParams.zw);
  r2.x = r2.x * r2.y;
  r1.xyzw = r2.xxxx * r1.xyzw;
  o0.xyzw = r1.xyzw * float4(0.0109913312,0.0109913312,0.0109913312,0.0109913312) + r0.xyzw;
}