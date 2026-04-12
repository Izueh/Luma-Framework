#pragma once

#include "safetyhook.hpp"

struct GBFRHookGlobals
{
   SafetyHookInline rt_creation_hook;
   SafetyHookInline update_screen_resolution_hook;
   SafetyHookInline vs_set_constant_buffers1_hook_immediate;
   SafetyHookInline vs_set_constant_buffers1_hook_deferred;
   safetyhook::MidHook jitter_write_hook;

   std::atomic<DeviceData*> device_data_ptr = nullptr;
   std::atomic<ID3D11Device*> native_device_ptr = nullptr;
   std::atomic<uint32_t> table_jitter_x_bits{0};
   std::atomic<uint32_t> table_jitter_y_bits{0};
   std::atomic<bool> table_jitter_valid{false};
};

constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;
constexpr uintptr_t kInitializeDX11RenderingPipeline_RVA = 0x00745510;
constexpr uintptr_t kUpdateScreenResolution_RVA = 0x005F7960;
constexpr uintptr_t kRenderWidth_RVA = 0x05AA41E8;
constexpr uintptr_t kRenderHeight_RVA = 0x05AA41EC;
constexpr uintptr_t kCameraGlobal_RVA = 0x068B4F90;
constexpr uintptr_t kCameraProjectionDataOffset = 0x60;
constexpr uintptr_t kProjectionJitterXOffset = 0x940;
constexpr uintptr_t kProjectionJitterYOffset = 0x944;
constexpr uintptr_t kTAASettingsGlobal_RVA = 0x05E55EA0;
constexpr uintptr_t kPauseCandidate_GlobalBit_RVA = 0x061720A4;
constexpr uintptr_t kPauseCandidate_TonemapGate_RVA = 0x05E5CABD;
constexpr uintptr_t kPauseCandidate_DofGateA_RVA = 0x06130C5C;
constexpr uintptr_t kPauseCandidate_DofGateB_RVA = 0x06130E13;
constexpr uintptr_t kJitterPhaseCounter_RVA = 0x05E61790;
constexpr uintptr_t kJitterPhaseMask_CL_RVA = 0x01A9EB76;
constexpr uintptr_t kJitterPhaseMask_EAX_RVA = 0x01A9EB7C;
constexpr uintptr_t kJitterWrite_RVA = 0x01A9EB9B;

inline GBFRHookGlobals g_hook_globals;
inline auto& g_rt_creation_hook = g_hook_globals.rt_creation_hook;
inline auto& g_update_screen_resolution_hook = g_hook_globals.update_screen_resolution_hook;
inline auto& g_VSSetConstantBuffers1_hook_immediate = g_hook_globals.vs_set_constant_buffers1_hook_immediate;
inline auto& g_VSSetConstantBuffers1_hook_deferred = g_hook_globals.vs_set_constant_buffers1_hook_deferred;
inline auto& g_jitter_write_hook = g_hook_globals.jitter_write_hook;
inline auto& g_device_data_ptr = g_hook_globals.device_data_ptr;
inline auto& g_native_device_ptr = g_hook_globals.native_device_ptr;

bool TryReadCameraJitter(float2& out_jitter);
void OnJitterWrite(safetyhook::Context& ctx);
bool TryReadTableJitter(float2& out_jitter);
void PatchJitterPhases();
bool IsTAARunningThisFrame();
float GetEffectiveRenderScale(bool taa_running);
void* GetVTableFunction(void* obj, size_t index);

char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height);
__int64 __fastcall Hooked_UpdateScreenResolution(__int64 a1);
void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Immediate(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants);
void STDMETHODCALLTYPE Hooked_VSSetConstantBuffers1_Deferred(
   ID3D11DeviceContext1* pContext,
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer* const* ppConstantBuffers,
   const UINT* pFirstConstant,
   const UINT* pNumConstants);
void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants);
