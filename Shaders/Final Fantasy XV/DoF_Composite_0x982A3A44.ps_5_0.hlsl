#include "Includes/Common.hlsl"

cbuffer cbDof : register(b0)
{
   float4 unprojectParam;
   float4 CoCParam0;
   float4 CoCParam1;
   float4 CoCParam2;
   float4 CoCParam3;
   float4 fullscreenDims;
   float4 vignetteBlurParam0;
   float4 vignetteBlurParam1;
   float4 uvJitterOffset;
   float4x4 motionMatrix;
   float blendBokeh_param0;
   float blendBokeh_param1;
   float gamePaused;
   float unused;
   float spare;
}

SamplerState linearSampler_s : register(s0);
Texture2D<float4> srcCoCTex : register(t0);
Texture2D<float4> blurredCoCTex : register(t1);
Texture2D<float4> srcBlurredTex : register(t2);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET0
{
   float nearCoC = blurredCoCTex.SampleLevel(linearSampler_s, uv, 0.0).x;
   nearCoC = min(CoCParam3.x, nearCoC * CoCParam2.x);
   // CoC remains on the jittered render grid; scene color has already passed AA.
   float farCoC = srcCoCTex.SampleLevel(linearSampler_s, uv + LumaData.GameData.TemporalJitterUV, 0.0).y;
   farCoC = min(CoCParam3.y, farCoC * CoCParam2.y);
   float coc = max(nearCoC, farCoC) * CoCParam0.w;
   if (coc < 1.5)
      discard;
   float alpha = LumaSettings.GameSettings.EnableDoF != 0 ? min(1.0, max(0.0, coc - blendBokeh_param0) * blendBokeh_param1) : 0.0;
   return float4(srcBlurredTex.SampleLevel(linearSampler_s, uv, 0.0).rgb, alpha);
}
