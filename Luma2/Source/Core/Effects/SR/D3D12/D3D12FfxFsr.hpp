#pragma once

#include <cstdint>

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Effects/SR/SuperResolution.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
// Native D3D12 FidelityFX API backend supporting both FSR 4.x and FSR 3.1.x via ffxOverrideVersion.
namespace FfxFsr {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   bool want_fsr4 = true;
   uint64_t version_id = 0;
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   HMODULE loader_module = nullptr;
   HMODULE upscaler_module = nullptr;
   void* fn_create_context = nullptr;
   void* fn_destroy_context = nullptr;
   void* fn_configure = nullptr;
   void* fn_query = nullptr;
   void* fn_dispatch = nullptr;
   void* context = nullptr;
   Hooks::Lifetime::D3D12::Object context_lifetime;
   Microsoft::WRL::ComPtr<ID3D12Resource> default_exposure;
   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> default_exposure_rtv;
   int phase_count = 16;
   bool spoof_amd = false;
   uint32_t logged_errors = 0;
};

bool Init(State& state, ID3D12Device* device, bool want_fsr4);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D12GraphicsCommandList* list, const SettingsData& settings);
bool Evaluate(State& state, ID3D12GraphicsCommandList* list, const DrawInputs<ID3D12Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D12GraphicsCommandList, ID3D12Resource>);
} // namespace FfxFsr
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
