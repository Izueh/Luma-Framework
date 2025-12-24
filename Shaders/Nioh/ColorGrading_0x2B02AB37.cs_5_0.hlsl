#include "../Includes/Common.hlsl"

#ifndef ENABLE_HDR_COLOR_GRADING
#define ENABLE_HDR_COLOR_GRADING 1
#endif

cbuffer ColorCorrectionConstants : register(b3)
{
  float4 g_cbColorCorrectionMatrix[3] : packoffset(c0);
  uint2 g_cbTexSize : packoffset(c3);
  uint2 padding : packoffset(c3.z);
}

Texture2D<float4> inputTex : register(t0);
RWTexture2D<float4> outputTex : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  if (vThreadID.x < g_cbTexSize.x && vThreadID.y < g_cbTexSize.y) {
    float4 r0;
    r0.xyz = inputTex.Load(int3(vThreadID.xy, 0)).xyz;
#if 1 // Luma: optionally disable clamping
    r0.yzw = max(float3(0,0,0), r0.xzy);
#else
    r0.yzw = r0.xzy;
#endif
#if ENABLE_HDR_COLOR_GRADING
    r0.x = dot(r0.ywz, g_cbColorCorrectionMatrix[0].xyz);
    r0.y = dot(r0.xwz, g_cbColorCorrectionMatrix[1].xyz);
    r0.z = dot(r0.xyz, g_cbColorCorrectionMatrix[2].xyz);
#else
    r0.xzy = r0.yzw;
#endif // ENABLE_HDR_COLOR_GRADING
    r0.w = 1;
    outputTex[vThreadID.xy] = r0.xyzw;
  }
}