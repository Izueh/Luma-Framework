#pragma once

#include <cstdint>

#include <Windows.h>
#include <d3d11.h>

#include "Effects/SR/SuperResolution.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace D3D12Interop {
struct Context;
}

namespace XeSS {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   D3D12Interop::Context* interop = nullptr;
   HMODULE module = nullptr;
   bool native_dx11 = false;
   void* fn_init_11 = nullptr;
   void* fn_execute_11 = nullptr;
   void* fn_init_12 = nullptr;
   void* fn_execute_12 = nullptr;
   void* fn_destroy_context = nullptr;
   void* fn_get_optimal = nullptr;
   void* fn_set_velocity_scale = nullptr;
   void* context = nullptr;
   bool context_initialized = false;
   uint32_t logged_errors = 0;
};

bool Init(State& state, ID3D11Device* device, D3D12Interop::Context* interop);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D11DeviceContext* context, const SettingsData& settings);
bool Evaluate(State& state, ID3D11DeviceContext* context, const DrawInputs<ID3D11Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D11DeviceContext, ID3D11Resource>);
} // namespace XeSS
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
