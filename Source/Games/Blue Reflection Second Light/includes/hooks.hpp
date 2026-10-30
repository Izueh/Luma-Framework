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

struct ScreenProjection
{
   float x;
   float y;
   float depth;
   int visible;
};

struct CCamera
{
   uintptr_t* owner;
   uintptr_t* vtable;
   float4 camera_direction;
   float4 camera_position;
   float4 camera_up_vector;
   DirectX::XMMATRIX view_matrix_prev; //not sure
   DirectX::XMMATRIX view_matrix;
   DirectX::XMMATRIX projection_matrix;
   DirectX::XMMATRIX view_projection_matrix;
   float2 camera_distances;
   float2 camera_fov;
};

struct CDepthBufferContainer
{
   uintptr_t* owner;
   uint64_t ref_count;
   ID3D11Texture2D* tex;
   ID3D11ShaderResourceView* srv;
};

extern float2 projection_jitters;
extern float2 output_resolution;
extern bool is_search_mode_on;
extern bool is_in_battle_mode;
extern bool is_hatching_on;
CCamera* CameraData = nullptr;
CDepthBufferContainer* DepthBuffer = nullptr;
uintptr_t RenderResolution = 0;

inline SafetyHookInline g_camera_fieldmap_view_proj_matrix_hook;
inline SafetyHookInline g_compute_projection_matrix_hook;
inline SafetyHookInline g_battle_camera_hook;
inline SafetyHookInline g_active_camera_hook;
inline SafetyHookInline g_search_mode_hook;
inline SafetyHookInline g_battle_mode_hook;
inline SafetyHookInline g_postfx_hatching_hook;

using fnCombineViewProjectionMatrices = char(__fastcall*)(__int64 a1, char a2, char a3);
fnCombineViewProjectionMatrices CombineViewProjectionMatrices = nullptr;

using fnGetSettingValue = __int64(__fastcall*)(unsigned int a1);
fnGetSettingValue GetSettingValue = nullptr;

__int64 __fastcall Hooked_ComputeProjectionMatrix(__int64 a1, float a2, float a3, float a4, float a5, char a6, float a7);
char __fastcall Hooked_CameraUpdateFieldMap3DHUD(__int64 a1);
__int64 __fastcall Hooked_CameraUpdateFunctionSceneAnd3DHUD(void *a1, __int64 a2, __int32 *a3);
__int64 __fastcall Hooked_UpdateActiveCameraAddress(__int64 a1, __int64 a2);
char __fastcall Hooked_BattleMainLoopIterate(__int64 a1);
__int64 __fastcall PostEffectHatchingRendererPrepare(__int64 a1, float a2);
void **__fastcall PostEffectSearchModeRendererPrepare(__int64 a1, float a2);