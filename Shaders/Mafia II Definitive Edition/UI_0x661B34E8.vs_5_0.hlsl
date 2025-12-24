#include "../Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 ucp0_ClipPlane : packoffset(c29);
  float4 d007_PosDecompressionScaleAndOffset : packoffset(c56);
}

void main(
  uint4 v0 : POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  uint vertexIdx : SV_VertexID,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float4 o2 : TEXCOORD1,
  out float4 o3 : SV_ClipDistance0)
{
  float4 r0,r1;
  r0.xyzw = (uint4)v0.xyzw;
  r1.xy = d007_PosDecompressionScaleAndOffset.ww * float2(1,256);
  r0.x = dot(r0.xy, r1.xy);
  r0.y = dot(r0.zw, r1.xy);
  r0.xy = d007_PosDecompressionScaleAndOffset.xy + r0.xy;
  r0.z = 1 + -r0.y;
  r0.xy = r0.xz * float2(2,2) + float2(-1,-1);
  
  o0.xy = r0.xy;
  o0.zw = float2(0,1);

  o1.xy = v2.xy;
  o1.zw = float2(0,0);

  o2.xyzw = v1.zyxw; // Color
  
  // Force the draw to be fullscreen, ignoring the vertices
  bool isFullscreenVideo = LumaData.CustomData1 != 0;
  if (isFullscreenVideo)
  {
    float2 uv = v2.xy;
    // clip space from uv (flip Y because uv.y grows downwards)
    o0.xy = float2(uv * float2(2, -2) + float2(-1, 1));
    o1.xy = uv;
  }
  
  o3.xyzw = dot(float3(o0.xy, 1.0), ucp0_ClipPlane.xyw);
}