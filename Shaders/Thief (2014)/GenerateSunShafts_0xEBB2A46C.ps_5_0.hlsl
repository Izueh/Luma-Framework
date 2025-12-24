Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[13];
}

#if 1 // Luma // TODO: expose
#define SAMPLES_NUM 128
#else // Vanilla
#define SAMPLES_NUM 64
#endif

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  r0.xyzw = cb0[11].zwzw * v0.xyxy;
  r1.xyzw = -v0.xyxy * cb0[11].zwzw + cb0[5].xyxy;
  r2.x = dot(r1.zw, r1.zw);
  r2.x = sqrt(r2.x);
  r2.x = 0.5 * r2.x;
  r1.xyzw = r1.xyzw / r2.x;
  r2.y = sqrt(r2.x);
  r2.y = 0.5 * r2.y;
  r2.x = min(r2.y, r2.x);
  r1.xyzw = r2.x * r1.xyzw;
  r2.x = (1.0 / SAMPLES_NUM) * cb0[12].z;
  r1.xyzw = r2.x * r1.xyzw;
  r0.xyzw = r1.zwzw * float4(0,0,1,1) + r0.xyzw;
  r1.xyzw = cb0[11].xyxy * r1.xyzw;
  r0.xyzw = cb0[11].xyxy * r0.xyzw;
  r2.xyzw = float4(0,0,0,0);
  r3.xyzw = r0.xyzw;
  r4.xw = float2(2, 2.0 - (2.0 / SAMPLES_NUM));
  int4 r5i;
  r5i.x = 0;
  while (true) {
    if (r5i.x >= SAMPLES_NUM) break;
    r4.zw = r4.xw;
    r5.yz = r4.zw * r4.zw;
    r5.yz = float2(4,4) * r5.yz;
    r4.xy = min(r5.yz, r4.zw);
    r6.xyzw = max(cb0[10].xyxy, r3.xyzw);
    r6.xyzw = min(cb0[10].zwzw, r6.xyzw);
    r7.xyzw = t0.Sample(s0_s, r6.xy).xyzw;
    r7.xyzw = r7.xyzw * r4.xxxz + r2.xyzw;
    r6.xyzw = t0.Sample(s0_s, r6.zw).xyzw;
    r2.xyzw = r6.xyzw * r4.yyyw + r7.xyzw;
    r4.xw = r4.zw - (4.0 / SAMPLES_NUM);
    r3.xyzw = r1.xyzw * 2.0 + r3.xyzw;
    r5i.x = r5i.x + 2;
  }
  o0.xyzw = r2.xyzw / float(SAMPLES_NUM);
}