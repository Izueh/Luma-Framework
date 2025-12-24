cbuffer _Globals : register(b0)
{
  float4 litDir : packoffset(c0);
  float4 litCol : packoffset(c1);
  float4 ambLit : packoffset(c2);
  float4 vEye : packoffset(c3);
  float4 Scat[4] : packoffset(c4);
  float fNoiseScale : packoffset(c8);
  float4 vTraceParams : packoffset(c9);
  float4 vTraceDeltaScale : packoffset(c10);
}

SamplerState __smpsCloud_s : register(s0);
SamplerState __smpsNoise_s : register(s1);
Texture2D<float4> sCloud : register(t0);
Texture2D<float4> sNoise : register(t1);

// Clouds can be a bit blue if we don't tonemap by channel, however that's mostly due to color grading
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float2 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.xyz = vEye.xyz + -v1.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.xyzw = sCloud.Sample(__smpsCloud_s, v2.xy).xyzw;
  r0.w = sNoise.Sample(__smpsNoise_s, v2.zw).x;
  r0.w = -0.5 + r0.w;
  r0.w = saturate(fNoiseScale * r0.w + v3.w);
  r2.xyzw = float4(-0.25,-0.5,-0.75,-1) + r0.wwww;
  r2.xyzw = -abs(r2.xyzw) * float4(4,4,4,4) + float4(1,1,1,1);
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r0.w = dot(r2.xyzw, r1.xyzw);
  r1.xyz = vTraceParams.xxx * litDir.xyz;
  r1.w = min(-9.99999975e-005, litDir.y);
  r1.xyz = r1.xyz / r1.www;
  r2.xyzw = vTraceDeltaScale.xyzw * r1.xzxz;
  r1.w = dot(r2.xy, r2.xy);
  r3.xy = sqrt(r1.ww);
  r1.w = dot(r2.zw, r2.zw);
  r3.zw = sqrt(r1.ww);
  r4.xyzw = min(vTraceParams.yyyy, r3.yyww);
  r3.xyzw = r4.xyzw / r3.xyzw;
  r2.xyzw = r3.xyzw * r2.xyzw;
  r2.xyzw = float4(0.0666666701,0.0666666701,0.0666666701,0.0666666701) * r2.xyzw;
  r1.w = min(0.0625, r0.w);
  r3.x = r1.w;
  int r3yi = 1;
  while (true) {
    r3.z = (r3yi >= 16);
    if (r3.z != 0) break;
    r3.z = r3yi;
    r4.xyzw = r2.xyzw * r3.zzzz + v2.xyzw;
    r5.xyzw = sCloud.Sample(__smpsCloud_s, r4.xy).xyzw;
    r3.w = sNoise.Sample(__smpsNoise_s, r4.zw).x;
    r3.w = -0.5 + r3.w;
    r3.w = saturate(fNoiseScale * r3.w + v3.w);
    r4.xyzw = float4(-0.25,-0.5,-0.75,-1) + r3.wwww;
    r4.xyzw = -abs(r4.xyzw) * float4(4,4,4,4) + float4(1,1,1,1);
    r4.xyzw = max(float4(0,0,0,0), r4.xyzw);
    r3.w = dot(r4.xyzw, r5.xyzw);
    r3.z = -r3.z * 0.0625 + r3.w;
    r3.z = max(0, r3.z);
    r3.z = min(0.0625, r3.z);
    r3.x = r3.x + r3.z;
    r3yi++;
  }
  r1.x = dot(r1.xyz, r1.xyz);
  r1.x = sqrt(r1.x);
  r1.x = r3.x * r1.x;
  r1.x = vTraceParams.z * -r1.x;
  r1.x = exp2(r1.x);
  r1.yzw = litCol.xyz * r1.xxx + ambLit.xyz;
  r1.x = vTraceParams.w * r1.x;
  r2.x = -1.44269502 * v4.y;
  r2.x = exp2(r2.x);
  r2.x = v4.x * r2.x;
  r0.x = dot(r0.xyz, litDir.xyz);
  r2.xyz = Scat[0].xyz * r2.xxx;
  r2.xyz = exp2(r2.xyz);
  r2.xyz = min(float3(1,1,1), r2.xyz);
  r0.y = Scat[2].w * r0.x + Scat[3].w;
  r0.y = max(9.99999975e-005, r0.y);
  r0.y = log2(r0.y);
  r0.y = -1.5 * r0.y;
  r0.y = exp2(r0.y);
  r0.x = r0.x * r0.x + 1;
  r3.xyz = Scat[2].xyz * r0.xxx;
  r0.xyz = Scat[3].xyz * r0.yyy + r3.xyz;
  r0.xyz = Scat[1].xyz + r0.xyz;
  r0.xyz = r0.xyz * r1.xxx;
  r1.xyz = v3.xyz * r1.yzw + -r0.xyz;
  o0.xyz = r2.xyz * r1.xyz + r0.xyz;
  o0.w = r0.w;
}