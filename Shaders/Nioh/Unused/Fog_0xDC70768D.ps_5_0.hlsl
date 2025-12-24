cbuffer _Globals : register(b0)
{
  float4 litDir[4] : packoffset(c0);
  float4 litCol[4] : packoffset(c4);
  float4 vEye : packoffset(c8);
  float4 cHFog[3] : packoffset(c9);
  float4 hFogParam[3] : packoffset(c12);
  float hFogFlucPrms[60] : packoffset(c15);
  int nClstPtch[2] : packoffset(c75);
  float4 vClstZPrm : packoffset(c77);
  float4 fog : packoffset(c78);
  float4 Scat[4] : packoffset(c79);
  row_major float4x4 mV2W : packoffset(c83);
  row_major float4x4 mP2W : packoffset(c87);
  float4 vAmbParam : packoffset(c91);
  float4 vAmbOccRat : packoffset(c92);
  float4 vAmbSpcWgt : packoffset(c93);
  float fWgtScl : packoffset(c94);
}

SamplerState __smpsRLR_s : register(s5);
SamplerState __smpsAmbSpc0_s : register(s6);
Texture2D<float4> sDepth : register(t0);
Texture2D<float4> sGBuf0 : register(t1);
Texture2D<float4> sGBuf1 : register(t2);
Texture2D<float4> sGBuf2 : register(t3);
Texture2D<float4> sScene : register(t4);
Texture2D<float4> sRLR : register(t5);
TextureCube<float4> sAmbSpc0 : register(t6);

#define cmp

// TODO: this decompile seems broken. I tried it as an attempt to fix the fog raising blacks, but it doesn't really seem to be needed. Fog seems to flicker a lot in some levels (the volumetric 3D one), especially in UW
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 r0u, r2u;
  r0.xy = v1.xy * float2(0.5,-0.5) + float2(0.5,0.5);
  int4 r1i;
  r1i.xy = (int2)v0.xy;
  r1i.zw = 0;
  r2.xyz = sGBuf1.Load(r1i.xyw).xyz;
  r0.z = sGBuf0.Load(r1i.xyw).w;
  r3.xyzw = sGBuf2.Load(r1i.xyw).xyzw;
  r2.xyz = float3(255,0.996336997,0.996336997) * r2.zxy;

  r0u.w = r2.x; // ftou
  r2u.x = r0u.w & 15; // and
  r2.x = r2u.x; // utof
  r2.x = r2.x * 0.000244200259 + r2.y; // mad
  r0u.w = r0u.w >> 4; // ushr
  r0.w = r0u.w; // utof

  r2.y = r0.w * 0.000244200259 + r2.z;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r4.xyz = float3(1,1,1) + -abs(r2.xyx);
  r5.z = r4.x + -abs(r2.y);
  r0.w = cmp(r5.z >= 0);
  r2.zw = cmp(r2.xy >= float2(0,0));
  r2.zw = r2.zw ? float2(1,1) : float2(-1,-1);
  r2.zw = r4.yz * r2.zw;
  r5.xy = r0.ww ? r2.xy : r2.zw;
  r0.w = dot(r5.xyz, r5.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r5.xyz * r0.www;
  r4.xyz = mV2W._m10_m11_m12 * r2.yyy;
  r2.xyw = r2.xxx * mV2W._m00_m01_m02 + r4.xyz;
  r2.xyz = r2.zzz * mV2W._m20_m21_m22 + r2.xyw;
  r0.w = sDepth.Load(r1i.xyw).x;
  r4.xyzw = mP2W._m10_m11_m12_m13 * v1.yyyy;
  r4.xyzw = v1.xxxx * mP2W._m00_m01_m02_m03 + r4.xyzw;
  r4.xyzw = r0.wwww * mP2W._m20_m21_m22_m23 + r4.xyzw;
  r4.xyzw = mP2W._m30_m31_m32_m33 + r4.xyzw;
  r4.xyz = r4.xyz / r4.www;
  r5.xyz = mV2W._m30_m31_m32 + -r4.xyz;
  r2.w = dot(mV2W._m20_m21_m22, r5.xyz);
  r2.w = r2.w * fog.x + fog.y;
  r5.xyz = vEye.xyz + -r4.xyz;
  r4.w = dot(r5.xyz, r5.xyz);
  r4.w = rsqrt(r4.w);
  r5.xyz = r5.xyz * r4.www;
  r4.w = saturate(dot(r2.xyz, r5.xyz));
  r5.w = r4.w + r0.z;
  r5.w = r5.w * r5.w + r0.z;
  r5.w = saturate(-1 + r5.w);
  r0.w = r0.w * vClstZPrm.x + vClstZPrm.y;
  r0.w = 1 / r0.w;
  r6.xyz = max(r3.www, r3.xyz);
  r6.xyz = r6.xyz + -r3.xyz;
  r4.w = 1 + -r4.w;
  r6.w = r4.w * r4.w;
  r6.w = r6.w * r6.w;
  r4.w = r6.w * r4.w;
  r3.xyz = r6.xyz * r4.www + r3.xyz;
  r3.w = -vAmbParam.z * r3.w + vAmbParam.z;
  r4.w = dot(-r5.xyz, r2.xyz);
  r4.w = r4.w + r4.w;
  r2.xyz = r2.xyz * -r4.www + -r5.xyz;
  r2.xyz = sAmbSpc0.SampleLevel(__smpsAmbSpc0_s, r2.xyz, r3.w).xyz;
  r2.xyz = vAmbSpcWgt.xxx * r2.xyz;
  r6.xyz = vAmbParam.yyy * r2.xyz;
  r7.xyzw = sRLR.SampleLevel(__smpsRLR_s, r0.xy, r3.w).xyzw;
  r0.x = fWgtScl * r7.w;
  r2.xyz = -r2.xyz * vAmbParam.yyy + r7.xyz;
  r2.xyz = r0.xxx * r2.xyz + r6.xyz;
  r6.xyz = r3.xyz * r2.xyz;
  r0.x = r0.z * vAmbOccRat.z + vAmbOccRat.w;
  r6.xyz = r6.xyz * r0.xxx;
  r2.xyz = r3.xyz * r2.xyz + -r6.xyz;
  r2.xyz = r5.www * r2.xyz + r6.xyz;
  r1.xyzw = sScene.Load(r1i.xyz).xyzw;
  r0.xyz = r2.xyz * r0.xxx + r1.xyz;
  r1.xy = float2(0,0);
  r1i.xy = 0;
  while (true) {
    r1.z = cmp(r1i.y >= 20);
    if (r1.z != 0) break;
    iadd r3.xyzw, r1.yyyy, l(16, 17, 18, 19)
    r1.z = r4.x * hFogFlucPrms[r1i.y+16] + hFogFlucPrms[r1i.y+17];
    r1.z = 6.28318501 * r1.z;
    r1.z = cos(r1.z);
    r2.x = r4.z * hFogFlucPrms[r1i.y+18] + hFogFlucPrms[r1i.y+19];
    r2.x = 6.28318501 * r2.x;
    r2.x = cos(r2.x);
    r1.z = r2.x + r1.z;
    r1.x = r1.z * hFogFlucPrms[r1i.y] + r1.x;
    r1i.y += 5;
  }
  r1.x = r4.y + r1.x;
  r1.x = saturate(r1.x * hFogParam[0].x + hFogParam[0].y);
  r1.y = saturate(cHFog[0].w);
  r1.x = r1.x * r1.y;
  r1.y = saturate(r0.w * hFogParam[0].z + hFogParam[0].w);
  r1.x = r1.x * r1.y;
  r2.xyz = cHFog[0].xyz + -r0.xyz;
  r0.xyz = r1.xxx * r2.xyz + r0.xyz;
  r1.xy = float2(0,0);
  r1i.xy = 0;
  while (true) {
    r1.z = cmp(r1i.y >= 20);
    if (r1.z != 0) break;
    iadd r3.xyzw, r1.yyyy, l(36, 37, 38, 39)
    r1.z = r4.x * hFogFlucPrms[r1i.y+36] + hFogFlucPrms[r1i.y+37];
    r1.z = 6.28318501 * r1.z;
    r1.z = cos(r1.z);
    r2.x = r4.z * hFogFlucPrms[r1i.y+38] + hFogFlucPrms[r1i.y+39];
    r2.x = 6.28318501 * r2.x;
    r2.x = cos(r2.x);
    r1.z = r2.x + r1.z;
    r1.x = r1.z * hFogFlucPrms[r1i.y+35] + r1.x;
    r1i.y += 5;
  }
  r1.x = r4.y + r1.x;
  r1.x = saturate(r1.x * hFogParam[1].x + hFogParam[1].y);
  r1.y = saturate(cHFog[1].w);
  r1.x = r1.x * r1.y;
  r1.y = saturate(r0.w * hFogParam[1].z + hFogParam[1].w);
  r1.x = r1.x * r1.y;
  r2.xyz = cHFog[1].xyz + -r0.xyz;
  r0.xyz = r1.xxx * r2.xyz + r0.xyz;
  r1.xy = float2(0,0);
  r1i.xy = 0;
  while (true) {
    r1.z = cmp(r1i.y >= 20);
    if (r1.z != 0) break;
    iadd r3.xyzw, r1.yyyy, l(56, 57, 58, 59)
    r1.z = r4.x * hFogFlucPrms[r1i.y+56] + hFogFlucPrms[r1i.y+57];
    r1.z = 6.28318501 * r1.z;
    r1.z = cos(r1.z);
    r2.x = r4.z * hFogFlucPrms[r1i.y+58] + hFogFlucPrms[r1i.y+59];
    r2.x = 6.28318501 * r2.x;
    r2.x = cos(r2.x);
    r1.z = r2.x + r1.z;
    r1.x = r1.z * hFogFlucPrms[r1i.y+55] + r1.x;
    r1i.y += 5;
  }
  r1.x = r4.y + r1.x;
  r1.x = saturate(r1.x * hFogParam[2].x + hFogParam[2].y);
  r1.y = saturate(cHFog[2].w);
  r1.x = r1.x * r1.y;
  r0.w = saturate(r0.w * hFogParam[2].z + hFogParam[2].w);
  r0.w = r1.x * r0.w;
  r1.xyz = cHFog[2].xyz + -r0.xyz;
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r0.w = dot(r5.xyz, litDir[0].xyz);
  r1.xyz = Scat[0].xyz * r2.www;
  r1.xyz = saturate(exp2(r1.xyz));
  r2.x = Scat[2].w * r0.w + Scat[3].w;
  r2.x = max(9.99999975e-005, r2.x);
  r2.x = log2(r2.x);
  r2.x = -1.5 * r2.x;
  r2.x = exp2(r2.x);
  r0.w = r0.w * r0.w + 1;
  r2.yzw = Scat[2].xyz * r0.www;
  r2.xyz = Scat[3].xyz * r2.xxx + r2.yzw;
  r2.xyz = Scat[1].xyz + r2.xyz;
  r0.xyz = -r2.xyz + r0.xyz;
  o0.xyz = r1.xyz * r0.xyz + r2.xyz;
  o0.w = r1.w;
}