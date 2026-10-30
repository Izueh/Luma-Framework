#pragma once
#include <cstddef>
#include <cstdint>

#include <safetyhook.hpp>

template<typename T>
static inline T* ResolveRipRelative(void* instr, std::ptrdiff_t dispOffset, std::size_t instrSize)
{
   auto base = reinterpret_cast<uintptr_t>(instr);
   auto rel = *reinterpret_cast<int32_t*>(base + dispOffset);
   auto dest = base + instrSize + rel;
   return reinterpret_cast<T*>(dest);
}


struct CTexture
{
   uintptr_t* __vftable;
   uint8_t _unk_field_0[0x8];
   uintptr_t* m_levelViews;
   ID3D11Resource* m_nativeTexture;
   ID3D11ShaderResourceView* m_shaderResourceView;
   ID3D11UnorderedAccessView* m_uav;
   com_ptr<ID3D11DepthStencilView> m_depthStencilViews[2][2];
   ID3D11RenderTargetView* m_renderTargetView;
   uintptr_t* m_aliases;
   unsigned int m_hideMipLevels;
   unsigned int m_numTextures;
   bool m_isCubeMap;
   bool m_isMultisampled;
   bool m_isVolume;
   bool m_isReusable;
   // dont care about the rest..
};

struct CIndirectTexture {
   CTexture* m_texture; // 0x0  (size 0x8)
   const char * m_debugName; // 0x8  (size 0x8)
   unsigned int m_sizeX; // 0x10  (size 0x4)
   unsigned int m_sizeY; // 0x14  (size 0x4)
   unsigned int m_sizeZ; // 0x18  (size 0x4)
   unsigned int m_mipLevelCount; // 0x28  (size 0x4)
   unsigned int m_format; // 0x2C  (size 0x4)
   uint8_t __pad[0x4];
};

struct CCameraMatrices {
   DirectX::XMMATRIX m_viewMatrix; // 0x0  (size 0x40)
   DirectX::XMMATRIX m_viewMatrixInverse; // 0x40  (size 0x40)
   DirectX::XMMATRIX m_viewMatrixPure; // 0x80  (size 0x40)
   DirectX::XMMATRIX m_projectionMatrix; // 0xC0  (size 0x40)
   DirectX::XMMATRIX m_projectionMatrixPure; // 0x100  (size 0x40)
   DirectX::XMMATRIX m_projectionMatrixInverse; // 0x140  (size 0x40)
   DirectX::XMMATRIX m_viewProjectionMatrix; // 0x180  (size 0x40)
   DirectX::XMMATRIX m_viewProjectionMatrixInverse; // 0x1C0  (size 0x40)
};

struct CCamera
{
   CCameraMatrices m_cameraMatrix; // 0x0  (size 0x200)
   bool m_useAngles; // 0x200  (size 0x1)
   uint8_t _pad_201[0x3]; // padding
   float m_Angles[3]; // 0x204  (size 0xC)
   float m_ProjectionRatio; // 0x210  (size 0x4)
   float m_projectedSizeFactor; // 0x214  (size 0x4)
   float m_orthoProjectionSizeFactor; // 0x218  (size 0x4)
   int32_t m_ProjType; // 0x21C  (size 0x4)
   float m_position[3]; // 0x220  (size 0xC)
   float  m_frontVector[3]; // 0x22C  (size 0xC)
   float m_upVector[3]; // 0x238  (size 0xC)
   float m_leftVector[3]; // 0x244  (size 0xC)
   float m_nearClipDistance; // 0x250  (size 0x4)
   float m_farClipDistance; // 0x254  (size 0x4)
   float m_FOV; // 0x258  (size 0x4)
   uint8_t m_cullingFrustum[0x180];// 0x25C  (size 0x180)
};

struct CControlCamera
{
   CCamera m_camera; // 0x0  (size 0x3E0)
   float m_ViewSurfaceX; // 0x3E0  (size 0x4)
   float m_ViewSurfaceZ; // 0x3E4  (size 0x4)
   float m_userDirection[3]; // 0x3E8  (size 0xC)
   float m_userUp[3]; // 0x3F4  (size 0xC)
   float m_verticalFOV; // 0x400  (size 0x4)
   float m_jitter[2]; // 0x404  (size 0x8)
   uint8_t _pad_end[0x14]; // final padding (total size 0x420)
};

struct CSceneViewportPrivateDataMotionBlur
{
   CControlCamera m_lastPreviousCamera;
   CControlCamera m_lastCurrentCamera;
   float m_lastGameDeltaTime;
   uint8_t __pad[0xC];
};

struct CSceneViewportPrivateData
{
   uint64_t field_0;
   int m_viewportPosition[2];
   int m_viewportSize[2];
   float m_cullingCameraPositionForAnimSafeLOD[3];
   uint8_t _pad_24[0xC];
   CControlCamera m_cullingCamera;
   CControlCamera m_renderCamera;
   CControlCamera m_renderCameraFull;
   CSceneViewportPrivateDataMotionBlur m_motionBlur;
   uint8_t unknown_field0[0x88];
   unsigned int m_renderOnceFrameCount;
   unsigned int m_renderOnceMaxNumFrames;
   unsigned int m_renderCounter;
   /*
   uint8_t unknown_field1[0x2C];
   CTexture* unknown_texture;
   uint8_t unknown_field2[0x20];
   CTexture* m_textures[6];
   uint8_t unknown_field3[0x4];
   unsigned int m_TextureCount;
   CTexture* more_textures[10];
   */
};

struct CShaderParameterMatrix44
{
   uint8_t _unk_field_0[0x10];
   DirectX::XMFLOAT4X4 matrix;
};

struct CViewportShaderParameterProvider
{
   uint8_t _unk_field_0[0x30];
   CShaderParameterMatrix44 m_viewProjectionMatrix;
   CShaderParameterMatrix44 m_projectionMatrix;
   CShaderParameterMatrix44 m_viewMatrix;
   CShaderParameterMatrix44 m_invViewMatrix;
   CShaderParameterMatrix44 m_viewRotProjectionMatrix;
   CShaderParameterMatrix44 m_viewRotProjectionMatrixPure;
   CShaderParameterMatrix44 m_invProjectionMatrix;
   CShaderParameterMatrix44 m_previousViewProjectionMatrix;
   // dont care about the rest..
};

struct CDeferredFxRendererContextTextures
{
   CIndirectTexture* m_currFrameTexture; // 0x0  (size 0x8)
   CIndirectTexture* m_linearDepthTexture; // 0x8  (size 0x8)
   CIndirectTexture* m_smallDepthColorTexture; // 0x10  (size 0x8)
   CIndirectTexture* m_depthStencilSurface; // 0x18  (size 0x8)
   CIndirectTexture* m_motionVectors; // 0x20  (size 0x8)
   CIndirectTexture* m_normalsTexture; // 0x28  (size 0x8)
   uintptr_t* m_gBufferAOTexture; // 0x30  (size 0x8)
   uintptr_t* m_fullAOTexture; // 0x38  (size 0x8)
};

struct CDeferredFxAntialiasRendererS
{
   uint8_t field_0[0x18]; // 0x0  (size 0x18)
   bool m_useAsyncCopy; // 0x18  (size 0x1)
   uint8_t _pad_21[0x7]; // padding
   CIndirectTexture* m_currDeferredFXAntialiasFrameTexture; // 0x20  (size 0x8)
   uintptr_t* m_rendererHelpers; // 0x28  (size 0x8)
   uintptr_t* m_volatileTextureManager; // 0x30  (size 0x8)
   uintptr_t* m_clearTextureFrameJob; // 0x38  (size 0x8)
   uintptr_t* m_generateFrameJob; // 0x40  (size 0x8)
   uintptr_t* m_copyLuminanceFrameJob; // 0x48  (size 0x8)
   unsigned __int64 m_shaderID; // 0x50  (size 0x8)
   unsigned __int64 m_generateOption; // 0x58  (size 0x8)
   unsigned __int64 m_zeroTimeDeltaOption; // 0x60  (size 0x8)
   unsigned __int64 m_noPreviousFrameOption; // 0x68  (size 0x8)
   unsigned __int64 m_resolveOption; // 0x70  (size 0x8)
   unsigned __int64 m_luminanceCopyOption; // 0x78  (size 0x8)
   unsigned __int64 m_debugInvalidColoursOption; // 0x80  (size 0x8)
   unsigned __int64 m_gbufferMSOption; // 0x88  (size 0x8)
   unsigned __int64 m_gbufferMSOptimizationPerPixelOption; // 0x90  (size 0x8)
   unsigned __int64 m_gbufferMSOptimizationPerSampleOption; // 0x98  (size 0x8)
   bool m_donePrepare; // 0xA0  (size 0x1)
   bool m_executeClearTextureFrameJob; // 0xA1  (size 0x1)
   bool m_willRenderThisFrame; // 0xA2  (size 0x1)
   bool m_doExecuteLuminanceFrameJob; // 0xA3  (size 0x1)
   unsigned int m_previousResetRequests; // 0xA4  (size 0x4)
};

struct PerFrame
{
   float2 CurrJitters;
   float2 PrevJitters;
   DirectX::XMMATRIX CameraSpaceToPreviousProjectedSpace;
   DirectX::XMMATRIX PreviousViewRotProjectionMatrix;
   float4 PreviousCameraPosition;  // W is dummy
   DirectX::XMMATRIX ViewRotProjectionMatrix;
   float2 RenderResolution;
   int2 RenderResolutionInt;
   CIndirectTexture* LinearDepthTexture;
   float ExposureScale;
   float MeasuredExposureScale;
   bool IsCameraCut;
   
   void Reset()
   {
      CurrJitters = {0, 0};
      PrevJitters = {0, 0};

      CameraSpaceToPreviousProjectedSpace = DirectX::XMMatrixIdentity();
      PreviousViewRotProjectionMatrix = DirectX::XMMatrixIdentity();
      ViewRotProjectionMatrix = DirectX::XMMatrixIdentity();

      PreviousCameraPosition = {0, 0, 0, 0};

      RenderResolution = {0, 0};
      RenderResolutionInt = {0, 0};
      
      LinearDepthTexture = nullptr;
      
      ExposureScale = 1.0;
      MeasuredExposureScale = 1.0;

      IsCameraCut = false;
   }
};

enum AAOptions {
   OPTION_NO_AA,
   OPTION_FXAA,
   OPTION_SMAA = 5,
   OPTION_SMAA_T2X
};

struct DeferredContextBindState
{
   bool pre_record_state;
   bool post_record_state;
};

inline SafetyHookInline g_deferred_fx_antialias_renderer_hook;
inline SafetyHookInline g_net_hacking_renderer_hook;
inline SafetyHookInline g_finnish_commandlist_hook;
inline SafetyHookInline g_clear_state_hook;

extern uintptr_t* AAOptionBase;
extern uintptr_t CDeferredFxAntialiasRenderer;
extern uintptr_t* m_deferredFXRendererContext;
extern CSceneViewportPrivateData* m_viewportPrivateData;
extern CViewportShaderParameterProvider* m_viewportParamProvider;
extern CDeferredFxAntialiasRendererS* m_deferredFxAntialiasRenderer;
//extern CDeferredFxRendererContextTextures m_deferredFXRendererContextTextures;
extern CTexture* m_currDeferredFXAntialiasFrameTexture;
extern uintptr_t JitterTableOffset;
//extern std::unordered_map<ID3D11DeviceContext*, DeferredContextBindState> luma_buffer_bind_state;
bool ZeroTimeDelta;
PerFrame g_perFrame;

extern bool bIsNetHackingRendering;

AAOptions GetAAOption();
//float GetGameDeltaTime();
//void GetViewportSize();

using fnGetExistingSharedTexture = __int64(__fastcall*)(__int64 a1, unsigned int a2);
extern fnGetExistingSharedTexture GetExistingSharedTexture;

using fnFinishCommandList = HRESULT(__fastcall*)(
    ID3D11DeviceContext*,
    BOOL,
    ID3D11CommandList**
);
fnFinishCommandList FinishCommandList = nullptr;

using fnClearState = void(__fastcall*)(ID3D11DeviceContext*);
fnClearState ClearState = nullptr;

__int64 __fastcall Hooked_CDeferredFxAntialiasRendererPrepare(__int64 a1, uintptr_t* a2);
__int64 __fastcall Hooked_CNetHackingRendererPrepare(void* renderer, void* context, void* arg3, void* arg4, void* arg5, void* arg6, void* arg7, void* textureManager);
//HRESULT __fastcall Hooked_FinishCommandList(ID3D11DeviceContext* ctx, BOOL restoreState, ID3D11CommandList** commandList);
//void __fastcall Hooked_ClearState(ID3D11DeviceContext* context);