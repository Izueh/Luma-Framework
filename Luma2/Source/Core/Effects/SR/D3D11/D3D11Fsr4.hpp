#pragma once

#include <cstdint>

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "Effects/SR/SuperResolution.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace D3D12Interop {
struct Context;
}

namespace Fsr4 {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   D3D12Interop::Context* interop = nullptr;
   HMODULE loader_module = nullptr;
   HMODULE upscaler_module = nullptr;
   void* fn_create_context = nullptr;
   void* fn_destroy_context = nullptr;
   void* fn_query = nullptr;
   void* fn_dispatch = nullptr;
   void* context = nullptr;
   Microsoft::WRL::ComPtr<ID3D11Texture2D> default_exposure;
   int phase_count = 16;
   bool spoof_amd = false;
   uint32_t logged_errors = 0;
};

bool Init(State& state, ID3D11Device* device, D3D12Interop::Context* interop);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D11DeviceContext* context, const SettingsData& settings);
bool Evaluate(State& state, ID3D11DeviceContext* context, const DrawInputs<ID3D11Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D11DeviceContext, ID3D11Resource>);
} // namespace Fsr4
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
