#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 vConfigParam : packoffset(c0);
}

SamplerState __smpsScreen_s : register(s0);
Texture2D<float4> sScreen : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
#if 1
  if (LumaData.CustomData1) // Luma: this means a video was playing and that this shader would otherwise stretch the image from 16:9 to whatever aspect ratio... The UI also writes to 16:9 but properly sets the viewport, videos don't
  {
    float2 size;
    sScreen.GetDimensions(size.x, size.y);
    float targetAspectRatio = size.x / size.y;
    float currentAspectRatio = LumaSettings.SwapchainSize.x * LumaSettings.SwapchainInvSize.y; // Luma: fixed bloom being more stretched in ultrawide
    float2 aspectRatioScale = float2(currentAspectRatio / targetAspectRatio, 1.0);
    v1.xy = ((v1.xy - 0.5) * aspectRatioScale) + 0.5;
    if (any(v1.xy > 1) || any(v1.xy < 0)) // Black bars
    { 
      o0 = 0;
      return;
    }
  }
#endif
  r0.xyzw = sScreen.Sample(__smpsScreen_s, v1.xy).xyzw;
  // User gamma/brightness adjustment. Neutral and default at 1.
  o0.xyz = pow(abs(r0.xyz), vConfigParam.x) * sign(r0.xyz); // Luma: mirrored negative values
  o0.w = r0.w;
}