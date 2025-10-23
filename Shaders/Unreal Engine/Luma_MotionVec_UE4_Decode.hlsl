#include "includes/Common.hlsl"

Texture2D<float4> DepthTexture : register(t0);    // Depth buffer
Texture2D<float4> VelocityTexture : register(t1); // Velocity texture

#if CS
RWTexture2D<float2> OutVelocityCombinedTexture;
#endif

SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
   float4 cb1[140];
}

#define AA_CROSS 1
#define DILATE_MOTION_VECTORS 1

// This is how MVs were encoded in UE/FF7
float2 EncodeVelocityToTexture(float2 In)
{
   // 0.499f is a value smaller than 0.5f to avoid using the full range to use the clear color (0,0) as special value
   // 0.5f to allow for a range of -2..2 instead of -1..1 for really fast motions for temporal AA.
   // Texure is R16G16 UNORM
   return In * (0.499f * 0.5f) + (32767.0f / 65535.0f);
}
float2 DecodeVelocityFromTexture(float2 In)
{
#if 1
   return (In - (32767.0f / 65535.0f)) / (0.499f * 0.5f);
#else // MAD layout to help compiler. This is what UE/FF7 used but it's an unnecessary approximation
   const float InvDiv = 1.0f / (0.499f * 0.5f);
   return In * InvDiv - 32767.0f / 65535.0f * InvDiv;
#endif
}

float2 ViewportUVToScreenPos(float2 ViewportUV)
{
   return float2(2 * ViewportUV.x - 1, 1 - 2 * ViewportUV.y);
}

float4 SvPositionToScreenPosition(float4 SvPosition)
{
   // todo: is already in .w or needs to be reconstructed like this:
   //	SvPosition.w = ConvertFromDeviceZ(SvPosition.z);
   float2 ViewRectMin = float2(LumaData.GameData.ViewportRect.xy);
   float4 ViewSizeAndInvSize = LumaData.GameData.RenderResolution;
   // float2 ViewRectMin = float2(0, 0);
   // float4 ViewSizeAndInvSize = cb1[118];

   float2 PixelPos = SvPosition.xy - ViewRectMin.xy;

   // NDC (NormalizedDeviceCoordinates, after the perspective divide)
   float3 NDCPos = float3((PixelPos * ViewSizeAndInvSize.zw - 0.5f) * float2(2, -2), SvPosition.z);

   // SvPosition.w: so .w has the SceneDepth, some mobile code and the DepthFade material expression wants that
   return float4(NDCPos.xyz, 1) * SvPosition.w;
}
#if CS
[numthreads(8, 8, 1)]
void main(uint2 GroupId: SV_GroupID, uint2 DispatchThreadId: SV_DispatchThreadID, uint2 GroupThreadId: SV_GroupThreadID,
          uint GroupIndex: SV_GroupIndex)
#else
void main(float4 pos: SV_Position0, out float2 OutVelocityCombinedTexture: SV_Target0)
#endif
{

   // Extract viewport parameters from constant buffer
   uint2 Velocity_ViewportMin = (uint2)LumaData.GameData.ViewportRect.xy;
   uint2 Velocity_ViewportMax = (uint2)LumaData.GameData.ViewportRect.xy + (uint2)LumaData.GameData.RenderResolution.xy;
   float2 Velocity_ViewportSize = LumaData.GameData.RenderResolution.xy;
   float2 Velocity_ViewportSizeInverse = LumaData.GameData.RenderResolution.zw;
   float2 Velocity_ExtentInverse = LumaData.GameData.RenderResolution.zw;
   uint2 CombinedVelocity_ViewportMin = (uint2)LumaData.GameData.ViewportRect.xy;
   float2 CombinedVelocity_ViewportSize = LumaData.GameData.RenderResolution.xy;
   float2 CombinedVelocity_ViewportSizeInverse = LumaData.GameData.RenderResolution.zw;
   float2 TemporalJitterPixels =
      LumaData.GameData.JitterOffset.xy * LumaData.GameData.RenderResolution.xy * float2(0.5, -0.5);
   // Get current pixel position

#if !CS
   uint2 DispatchThreadId = (uint2)pos.xy;
#endif

   uint2 PixelPos = min(DispatchThreadId + Velocity_ViewportMin, Velocity_ViewportMax - 1);
   uint2 OutputPixelPos = (uint2)CombinedVelocity_ViewportMin + DispatchThreadId;
   // Check viewport bounds
   const bool bInsideViewport = all(PixelPos.xy < Velocity_ViewportMax);
   if (!bInsideViewport) return;
#if DILATE_MOTION_VECTORS
   // Screen position of minimum depth for motion vector dilation
   float2 VelocityOffset = float2(0.0, 0.0);

   float2 NearestBufferUV = (PixelPos + 0.5f) * Velocity_ViewportSizeInverse;
   float2 ViewportUV = (float2(DispatchThreadId) + 0.5f) * CombinedVelocity_ViewportSizeInverse;

   // Pixel coordinate of the center of output pixel O in the input viewport
   float2 PPCo = ViewportUV * Velocity_ViewportSize + TemporalJitterPixels;

   // Pixel coordinate of the center of the nearest input pixel K
   float2 PPCk = floor(PPCo) + 0.5;

   NearestBufferUV = Velocity_ExtentInverse * (Velocity_ViewportMin + PPCk);

   // FIND MOTION OF PIXEL AND NEAREST IN NEIGHBORHOOD
   float3 PosN; // Position of this pixel, possibly later nearest pixel in neighborhood
   PosN.xy = ViewportUVToScreenPos(ViewportUV);
   PosN.z = DepthTexture.SampleLevel(s0_s, NearestBufferUV, 0).x;

   // Motion vector dilation based on depth
   {
      // Sample depth in cross pattern to find nearest (foreground) pixel
      float4 Depths;
      Depths.x = DepthTexture.SampleLevel(s0_s, NearestBufferUV, 0, int2(-AA_CROSS, -AA_CROSS)).x;
      Depths.y = DepthTexture.SampleLevel(s0_s, NearestBufferUV, 0, int2(AA_CROSS, -AA_CROSS)).x;
      Depths.z = DepthTexture.SampleLevel(s0_s, NearestBufferUV, 0, int2(-AA_CROSS, AA_CROSS)).x;
      Depths.w = DepthTexture.SampleLevel(s0_s, NearestBufferUV, 0, int2(AA_CROSS, AA_CROSS)).x;

      float2 DepthOffset = float2(AA_CROSS, AA_CROSS);
      float DepthOffsetXx = float(AA_CROSS);

      // Find nearest depth (largest value in inverted Z buffer)
      if (Depths.x > Depths.y)
      {
         DepthOffsetXx = -AA_CROSS;
      }
      if (Depths.z > Depths.w)
      {
         DepthOffset.x = -AA_CROSS;
      }
      float DepthsXY = max(Depths.x, Depths.y);
      float DepthsZW = max(Depths.z, Depths.w);
      if (DepthsXY > DepthsZW)
      {
         DepthOffset.y = -AA_CROSS;
         DepthOffset.x = DepthOffsetXx;
      }
      float DepthsXYZW = max(DepthsXY, DepthsZW);
      if (DepthsXYZW > PosN.z)
      {
         // Offset for reading from velocity texture
         VelocityOffset = DepthOffset * Velocity_ExtentInverse;
         PosN.z = DepthsXYZW;
      }
   }

   // Calculate camera motion
   float4 ThisClip = float4(PosN.xy, PosN.z, 1);

   // Transform using View.ClipToPrevClip matrix from cb1[114-117]
   float4x4 ClipToPrevClip =
      float4x4(cb1[LumaData.GameData.ClipToPrevClipIndex], cb1[LumaData.GameData.ClipToPrevClipIndex + 1],
               cb1[LumaData.GameData.ClipToPrevClipIndex + 2], cb1[LumaData.GameData.ClipToPrevClipIndex + 3]);
   // float4x4 ClipToPrevClip = LumaData.GameData.ClipToPrevClip;
   float4 PrevClip = mul(ThisClip, ClipToPrevClip);
   float2 PrevScreen = PrevClip.xy / PrevClip.w;
   float2 BackN = PosN.xy - PrevScreen;

   float2 BackTemp = BackN * Velocity_ViewportSize;

   // Sample velocity texture with dilation offset
   float4 VelocityN = VelocityTexture.SampleLevel(s0_s, NearestBufferUV + VelocityOffset, 0);
   bool DynamicN = VelocityN.x > 0.0;
   if (DynamicN)
   {
      BackN = DecodeVelocityFromTexture(VelocityN.xy).xy;
   }
   BackTemp = BackN * CombinedVelocity_ViewportSize;

   // Output motion vector for DLSS
#if CS
   OutVelocityCombinedTexture[OutputPixelPos].xy = -BackTemp * float2(0.5, -0.5);
#else
   OutVelocityCombinedTexture.xy = -BackTemp * float2(0.5, -0.5);
#endif

#else // !DILATE_MOTION_VECTORS

   // Simple path without motion vector dilation
   float4 EncodedVelocity = VelocityTexture.Load(int3(PixelPos, 0));
   float Depth = DepthTexture.Load(int3(PixelPos, 0)).x;

   float2 Velocity;
   if (all(EncodedVelocity.xy > 0))
   {
      Velocity = DecodeVelocityFromTexture(EncodedVelocity.xy).xy;
   }
   else
   {
      // Calculate camera motion
      float4 ClipPos;
      ClipPos.xy = SvPositionToScreenPosition(float4(PixelPos.xy, 0, 1)).xy;
      ClipPos.z = Depth;
      ClipPos.w = 1;

      float4x4 ClipToPrevClip =
         float4x4(cb1[LumaData.GameData.ClipToPrevClipIndex], cb1[LumaData.GameData.ClipToPrevClipIndex + 1],
                  cb1[LumaData.GameData.ClipToPrevClipIndex + 2], cb1[LumaData.GameData.ClipToPrevClipIndex + 3]);
      // float4x4 ClipToPrevClip = LumaData.GameData.ClipToPrevClip;
      float4 PrevClipPos = mul(ClipPos, ClipToPrevClip);

      if (PrevClipPos.w > 0)
      {
         float2 PrevScreen = PrevClipPos.xy / PrevClipPos.w;
         Velocity = ClipPos.xy - PrevScreen.xy;
      }
      else
      {
         Velocity = EncodedVelocity.xy;
      }
   }

   float2 OutVelocity =
      Velocity * float2(0.5, -0.5) * LumaData.GameData.RenderResolution.xy; // View.ViewSizeAndInvSize.xy

   // Output motion vector for DLSS
#if CS
   OutVelocityCombinedTexture[OutputPixelPos].xy = -OutVelocity;
#else
   OutVelocityCombinedTexture.xy = -OutVelocity;
#endif

#endif // DILATE_MOTION_VECTORS
}
