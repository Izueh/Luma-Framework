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
namespace XeSS {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   HMODULE module = nullptr;
   void* fn_create_context = nullptr;
   void* fn_init_12 = nullptr;
   void* fn_execute_12 = nullptr;
   void* fn_destroy_context = nullptr;
   void* fn_get_optimal = nullptr;
   void* fn_set_velocity_scale = nullptr;
   void* context = nullptr;
   Hooks::Lifetime::D3D12::Object context_lifetime;
   bool context_initialized = false;
   uint32_t logged_errors = 0;
};

bool Init(State& state, ID3D12Device* device);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D12GraphicsCommandList* list, const SettingsData& settings);
bool Evaluate(State& state, ID3D12GraphicsCommandList* list, const DrawInputs<ID3D12Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D12GraphicsCommandList, ID3D12Resource>);
} // namespace XeSS
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
