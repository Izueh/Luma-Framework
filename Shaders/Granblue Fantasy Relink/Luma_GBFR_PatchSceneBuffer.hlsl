// Luma_GBFR_PatchSceneBuffer.hlsl
// Compute shader that patches the Granblue Fantasy Relink SceneBuffer (cbSceneBuffer)
// to replace engine TAA jitter in the projection matrix with our own DLSS/FSR jitter,
// then recomputes all derived matrices (ViewProjection, PrevViewProjection, etc.).
//
// The original SceneBuffer is bound as a constant buffer at slot b0.
// The patched result is written to a RWStructuredBuffer at u0.
// After dispatch, the caller uses CopySubresourceRegion to copy the patched data
// back into the global ring buffer at the correct offset.

#include "Includes/Common.hlsl"

// Match the cbSceneBuffer layout from cbuffers.h (544 bytes = 34 float4 registers)
// GBFR SceneBuffer matrices are consumed as column_major in HLSL.
struct SceneBuffer
{
   column_major float4x4 g_View;                   // Offset:   0  (c0-c3)
   column_major float4x4 g_Proj;                   // Offset:  64  (c4-c7)
   column_major float4x4 g_ViewProjection;         // Offset: 128  (c8-c11)
   column_major float4x4 g_ViewInverseMatrix;      // Offset: 192  (c12-c15)
   column_major float4x4 g_PrevView;               // Offset: 256  (c16-c19)
   column_major float4x4 g_PrevProj;               // Offset: 320  (c20-c23)
   column_major float4x4 g_PrevViewProjection;     // Offset: 384  (c24-c27)
   column_major float4x4 g_PrevViewInverseMatrix;  // Offset: 448  (c28-c31)
   float4   g_ProjectionOffset;       // Offset: 512  (c32) - jitter offsets
   int4     g_FrameCount;             // Offset: 528  (c33)
};

// The original SceneBuffer bound as a constant buffer (read-only input)
cbuffer OriginalSceneBuffer : register(b0)
{
   SceneBuffer g_SceneBuffer;
}

// Output: patched SceneBuffer written as a structured buffer
RWStructuredBuffer<SceneBuffer> g_PatchedSceneBuffer : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
   SceneBuffer patched = g_SceneBuffer;

   // =========================================================================
   // Patch current-frame projection matrix: replace engine jitter with ours
   // =========================================================================
   // The engine TAA jitter is stored in g_Proj._m20 and g_Proj._m21.
   // These are the asymmetric frustum offsets in NDC space.
   // We strip the engine jitter and apply our own from LumaData.

   float4x4 proj = patched.g_Proj;

   // Strip engine jitter first so we can apply Luma jitter deterministically.
   proj[2][0] = 0.0;
   proj[2][1] = 0.0;

   // Apply our jitter (from LumaData.GameData.JitterOffset, in NDC space)
   proj[2][0] = LumaData.GameData.JitterOffset.x;
   proj[2][1] = LumaData.GameData.JitterOffset.y;

   patched.g_Proj = proj;

   // Recompute ViewProjection = View * Proj
   patched.g_ViewProjection = mul(patched.g_View, patched.g_Proj);

   // =========================================================================
   // Patch previous-frame projection matrix similarly
   // =========================================================================
   float4x4 prevProj = patched.g_PrevProj;

   // Same replacement for previous-frame projection.
   prevProj[2][0] = 0.0;
   prevProj[2][1] = 0.0;

   // Apply the PREVIOUS frame's jitter to g_PrevProj (not the current frame's)
   prevProj[2][0] = LumaData.GameData.PrevJitterOffset.x;
   prevProj[2][1] = LumaData.GameData.PrevJitterOffset.y;

   patched.g_PrevProj = prevProj;

   // Recompute PrevViewProjection = PrevView * PrevProj
   patched.g_PrevViewProjection = mul(patched.g_PrevView, prevProj);

   // =========================================================================
   // Update g_ProjectionOffset to reflect the new jitter
   // =========================================================================
   // The pixel shaders unjitter NDC by adding g_ProjectionOffset:
   //   ndc_clean = ndc_jittered + g_ProjectionOffset
   // The jitter effect in NDC is -_m20 (due to perspective divide w = -z),
   // so g_ProjectionOffset must equal +_m20 (the raw projection matrix value)
   // to cancel it.  This is the SAME value we wrote into _m20/_m21, not negated.
   // .xy = Proj._m20/_m21  (current), .zw = PrevProj._m20/_m21 (previous)
   patched.g_ProjectionOffset.x = LumaData.GameData.JitterOffset.x;
   patched.g_ProjectionOffset.y = LumaData.GameData.JitterOffset.y;
   patched.g_ProjectionOffset.z = LumaData.GameData.PrevJitterOffset.x;
   patched.g_ProjectionOffset.w = LumaData.GameData.PrevJitterOffset.y;

   // Write patched SceneBuffer to output
   g_PatchedSceneBuffer[0] = patched;
}
