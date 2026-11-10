#include "..\..\Core\core.hpp"
#include "hooks.hpp"

uintptr_t* AAOptionBase = nullptr;
uintptr_t CDeferredFxAntialiasRenderer = 0;
uintptr_t* m_deferredFXRendererContext = nullptr;
//CDeferredFxRendererContextTextures m_deferredFXRendererContextTextures;
CSceneViewportPrivateData* m_viewportPrivateData = nullptr;
CViewportShaderParameterProvider* m_viewportParamProvider = nullptr;
CDeferredFxAntialiasRendererS* m_deferredFxAntialiasRenderer = nullptr;
CTexture* m_currDeferredFXAntialiasFrameTexture = nullptr;
uintptr_t JitterTableOffset = 0;

bool bIsNetHackingRendering = false;

fnGetExistingSharedTexture GetExistingSharedTexture = nullptr;

DirectX::XMMATRIX ComputeCameraSpaceToPreviousProjectedSpaceMatrix()
{
   using namespace DirectX;
	
   XMMATRIX inv_view_matrix_current = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_viewMatrixInverse));
   XMMATRIX inv_view_matrix_prev = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_viewMatrixInverse));
   XMMATRIX proj_prev = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_projectionMatrix));
		
   proj_prev = XMMatrixTranspose(proj_prev);
   
   float4 position_delta = {0.0,0.0,0.0,1.0};
   position_delta.x = inv_view_matrix_current.r[3].m128_f32[0] - inv_view_matrix_prev.r[3].m128_f32[0];
   position_delta.y = inv_view_matrix_current.r[3].m128_f32[1] - inv_view_matrix_prev.r[3].m128_f32[1];
   position_delta.z = inv_view_matrix_current.r[3].m128_f32[2] - inv_view_matrix_prev.r[3].m128_f32[2];
   
   inv_view_matrix_current = XMMatrixTranspose(inv_view_matrix_current);
   inv_view_matrix_prev = XMMatrixTranspose(inv_view_matrix_prev);
   
   XMMATRIX view_rotation_prev = inv_view_matrix_prev;
   view_rotation_prev.r[0].m128_f32[3] = 0.0;
   view_rotation_prev.r[1].m128_f32[3] = 0.0;
   view_rotation_prev.r[2].m128_f32[3] = 0.0;
   view_rotation_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
   
   {
      XMVECTOR c0 = view_rotation_prev.r[0];
      XMVECTOR c1 = view_rotation_prev.r[1];
      XMVECTOR c2 = view_rotation_prev.r[2];

      view_rotation_prev.r[0] = XMVectorSet(XMVectorGetX(c0), XMVectorGetX(c1), XMVectorGetX(c2), 0.0f);
      view_rotation_prev.r[1] = XMVectorSet(XMVectorGetY(c0), XMVectorGetY(c1), XMVectorGetY(c2), 0.0f);
      view_rotation_prev.r[2] = XMVectorSet(XMVectorGetZ(c0), XMVectorGetZ(c1), XMVectorGetZ(c2), 0.0f);
      view_rotation_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
   }
   
   XMMATRIX inv_view_delta_matrix_current = inv_view_matrix_current;
   inv_view_delta_matrix_current.r[0].m128_f32[3] = position_delta.x;
   inv_view_delta_matrix_current.r[1].m128_f32[3] = position_delta.y;
   inv_view_delta_matrix_current.r[2].m128_f32[3] = position_delta.z;
   
   XMMATRIX temp = XMMatrixMultiply(proj_prev, view_rotation_prev);
   return XMMatrixMultiply(temp, inv_view_delta_matrix_current);
}

DirectX::XMMATRIX ComputePreviousViewRotProjectionMatrix()
{
   using namespace DirectX;
   
   XMMATRIX view_matrix_prev = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_viewMatrix));
   XMMATRIX proj_prev = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_projectionMatrix));

   proj_prev = XMMatrixTranspose(proj_prev);
   view_matrix_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
   view_matrix_prev = XMMatrixTranspose(view_matrix_prev);

   return XMMatrixMultiply(proj_prev, view_matrix_prev);
}

DirectX::XMMATRIX ComputeViewRotProjectionPureMatrix()
{
   using namespace DirectX;
   
   XMMATRIX view_matrix = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_viewMatrix));
   XMMATRIX proj = XMLoadFloat4x4(
      reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_projectionMatrix));

   proj = XMMatrixTranspose(proj);
   proj.r[0].m128_f32[2] = 0.0f;
   proj.r[1].m128_f32[2] = 0.0f;
   view_matrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
   view_matrix = XMMatrixTranspose(view_matrix);

   return XMMatrixMultiply(proj, view_matrix);
}

AAOptions GetAAOption()
{
   return *(AAOptions*)(*(uintptr_t*)(*AAOptionBase) + 0x3A4);
}

__int64 __fastcall Hooked_CDeferredFxAntialiasRendererPrepare(__int64 a1, uintptr_t* a2)
{
   if (a1)
   {
      CDeferredFxAntialiasRenderer = a1;
      m_deferredFxAntialiasRenderer = reinterpret_cast<CDeferredFxAntialiasRendererS*>(a1);
      
      uintptr_t m_rendererHelpers = *reinterpret_cast<uintptr_t*>(a1 + 40);

      float game_time_delta = *reinterpret_cast<float*>(m_rendererHelpers + 1912) - *reinterpret_cast<float*>(m_rendererHelpers + 1916);
      ZeroTimeDelta = game_time_delta == 0.0f;
   }
   
   if (a2 && a2[13] && a2[8] && a2[1])
   {
      m_deferredFXRendererContext = (uintptr_t*)a2;
      uintptr_t base = a2[13];
      m_viewportPrivateData = reinterpret_cast<CSceneViewportPrivateData*>(base);
      base = a2[8];
      m_viewportParamProvider = reinterpret_cast<CViewportShaderParameterProvider*>(base);

#if 0
      {
         m_deferredFXRendererContextTextures.m_currFrameTexture = m_deferredFxAntialiasRenderer->m_currDeferredFXAntialiasFrameTexture;
         //m_deferredFXRendererContextTextures.m_accumBuffer = reinterpret_cast<CIndirectTexture*>(a2[0]);
         m_deferredFXRendererContextTextures.m_linearDepthTexture = reinterpret_cast<CIndirectTexture*>(a2[1]);
         m_deferredFXRendererContextTextures.m_smallDepthColorTexture = reinterpret_cast<CIndirectTexture*>(a2[2]);
         m_deferredFXRendererContextTextures.m_depthStencilSurface = reinterpret_cast<CIndirectTexture*>(a2[3]);
         
         //uintptr_t* motion_vector_base = (uintptr_t*)((uint8_t*)m_viewportPrivateData->m_textures[1] - 0x900);
         // this is actually unsafe, will crash between nethacking switch
         // my guess is that it adds ref to the resource and game needs to free it
         if (0)
         {
            auto textureManager = *reinterpret_cast<uintptr_t*>(a1 + 48);
            if (textureManager)
            {
               m_deferredFXRendererContextTextures.m_motionVectors = reinterpret_cast<CIndirectTexture*>(GetExistingSharedTexture(*(uintptr_t*)(a1 + 48), 931095925));
           }
         }
         m_deferredFXRendererContextTextures.m_normalsTexture = reinterpret_cast<CIndirectTexture*>(a2[5]);
         m_deferredFXRendererContextTextures.m_gBufferAOTexture = reinterpret_cast<uintptr_t*>(a2[6]);
         m_deferredFXRendererContextTextures.m_fullAOTexture = reinterpret_cast<uintptr_t*>(a2[7]);
      }
#endif
      {
         if (&m_viewportPrivateData->m_motionBlur != nullptr && m_viewportPrivateData != nullptr)
         {
            g_perFrame.CameraSpaceToPreviousProjectedSpace = ComputeCameraSpaceToPreviousProjectedSpaceMatrix();
            g_perFrame.PreviousViewRotProjectionMatrix = ComputePreviousViewRotProjectionMatrix();
            
            g_perFrame.PreviousCameraPosition = {
               m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[0],
               m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[1],
               m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[2],
               1.0,
            };
            
            g_perFrame.ViewRotProjectionMatrix = ComputeViewRotProjectionPureMatrix();
         
            float2 jitter = {0.0, 0.0};
            jitter.x = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[0] * (float)m_viewportPrivateData->m_viewportSize[0];
            jitter.y = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[1] * (float)m_viewportPrivateData->m_viewportSize[1];
            g_perFrame.CurrJitters = jitter;
         
            jitter.x = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_jitter[0] * (float)m_viewportPrivateData->m_viewportSize[0];
            jitter.y = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_jitter[1] * (float)m_viewportPrivateData->m_viewportSize[1];
            g_perFrame.PrevJitters = jitter;
            
            g_perFrame.RenderResolution.x = static_cast<float>(m_viewportPrivateData->m_viewportSize[0]);
            g_perFrame.RenderResolution.y = static_cast<float>(m_viewportPrivateData->m_viewportSize[1]);
            g_perFrame.RenderResolutionInt.x = m_viewportPrivateData->m_viewportSize[0];
            g_perFrame.RenderResolutionInt.y = m_viewportPrivateData->m_viewportSize[1];
            
            DirectX::XMVECTOR cam_dir_prev = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_viewMatrix.r[1];
            DirectX::XMVECTOR cam_dir_curr = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_viewMatrix.r[1];
            float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(cam_dir_prev, cam_dir_curr));
            g_perFrame.IsCameraCut = dot < 0.80000001;
            
            g_perFrame.ExposureScale = *reinterpret_cast<float*>(a2[8] + 0xD20);
            g_perFrame.MeasuredExposureScale = *reinterpret_cast<float*>(a2[8] + 0xD40);
            
            g_perFrame.LinearDepthTexture = reinterpret_cast<CIndirectTexture*>(a2[1]);
         }
      }
   }

   auto original_result = g_deferred_fx_antialias_renderer_hook
       .unsafe_call<__int64>(a1, a2);
   
   return original_result;
}

__int64 __fastcall Hooked_CNetHackingRendererPrepare(void* renderer, void* context, void* arg3, void* arg4, void* arg5, void* arg6, void* arg7, void* textureManager)
{
   auto original_result = g_net_hacking_renderer_hook
       .unsafe_call<__int64>(renderer, context, arg3, arg4, arg5, arg6, arg7, textureManager);

   bIsNetHackingRendering = true;

   return original_result;
}
#if 0
HRESULT __fastcall Hooked_FinishCommandList(ID3D11DeviceContext* ctx, BOOL restoreState, ID3D11CommandList** commandList)
{
   //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, std::format("ID3D11DeviceContext::FinishCommandList - restoreState = {}", restoreState).c_str());
   HRESULT hr = FinishCommandList(ctx, restoreState, commandList);

   auto bind = luma_buffer_bind_state.find(ctx);
   if (bind != luma_buffer_bind_state.end())
   {
      if (restoreState)
      {
         bind->second.post_record_state = bind->second.pre_record_state;
      }
      else
      {
         bind->second.pre_record_state = bind->second.post_record_state;
      }
   }
   
   return hr;
}

void __fastcall Hooked_ClearState(ID3D11DeviceContext* context)
{
   //Luma::Utils::Log::Write(Luma::Utils::Log::Level::Info, "ID3D11DeviceContext::ClearState");
   ClearState(context);
   
   auto it = luma_buffer_bind_state.find(context);

   if (it != luma_buffer_bind_state.end())
   {
      it->second.pre_record_state = false;
      it->second.post_record_state = false;
   }
}
#endif