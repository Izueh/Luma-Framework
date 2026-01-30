#include "common.hlsl"
// ---- Created with 3Dmigoto v1.4.1 on Sat Jan 24 01:50:35 2026

// cbuffer cbMotMat : register(b0)
// {
//     float4x4 g_motionMatrix : packoffset(c0);
//     float4 g_jitterOfs : packoffset(c4);
// }

SamplerState pointSampler_s : register(s0);
Texture2D<float> g_depthTex : register(t0);
Texture2D<float2> g_velocityTex : register(t1);

// 3Dmigoto declarations
#define cmp -

void main(
    float4 v0: SV_POSITION0,
    float2 v1: TEXCOORD0,
    out float4 o0: SV_TARGET0)
{
    float4 r0, r1, r2, r3;
    uint4 bitmask, uiDest;
    float4 fDest;

    g_velocityTex.GetDimensions(0, fDest.x, fDest.y, fDest.z);
    r0.xy = fDest.xy;
    r0.xy = v1.xy * r0.xy;
    r0.xy = (int2)r0.xy;
    r0.zw = float2(0, 0);
    r0.xy = g_velocityTex.Load(r0.xyz).xy;
    r0.z = cmp(r0.y == 1.000000);
    if (r0.z != 0) {
        r1.z = g_depthTex.SampleLevel(pointSampler_s, v1.xy, 0).x;
        r2.x = LumaData.GameData.motMat.g_motionMatrix._m00;
        r2.y = LumaData.GameData.motMat.g_motionMatrix._m01;
        r2.z = LumaData.GameData.motMat.g_motionMatrix._m02;
        r2.w = LumaData.GameData.motMat.g_motionMatrix._m03;
        r1.xy = v1.xy;
        r1.w = 1;
        r2.x = dot(r2.xyzw, r1.xyzw);
        r3.x = LumaData.GameData.motMat.g_motionMatrix._m10;
        r3.y = LumaData.GameData.motMat.g_motionMatrix._m11;
        r3.z = LumaData.GameData.motMat.g_motionMatrix._m12;
        r3.w = LumaData.GameData.motMat.g_motionMatrix._m13;
        r2.y = dot(r3.xyzw, r1.xyzw);
        r3.x = LumaData.GameData.motMat.g_motionMatrix._m30;
        r3.y = LumaData.GameData.motMat.g_motionMatrix._m31;
        r3.z = LumaData.GameData.motMat.g_motionMatrix._m32;
        r3.w = LumaData.GameData.motMat.g_motionMatrix._m33;
        r0.z = dot(r3.xyzw, r1.xyzw);
        r0.zw = r2.xy / r0.zz;
        r0.xy = -v1.xy + r0.zw;
    } else {
        r0.z = cmp(abs(r0.x) >= 4);
        r0.w = cmp(0 < r0.x);
        r1.x = cmp(r0.x < 0);
        r0.w = (int)-r0.w + (int)r1.x;
        r0.w = (int)r0.w;
        r0.w = -4 * r0.w;
        r0.z = r0.z ? r0.w : 0;
        r0.x = r0.x + r0.z;
    }
    r0.xy = LumaData.GameData.motMat.g_jitterOfs.xy * float2(0.5, 0.5) + r0.xy;
    o0.xy = LumaData.GameData.motMat.g_jitterOfs.zw * r0.xy;
    o0.zw = float2(0, 0);
    return;
}