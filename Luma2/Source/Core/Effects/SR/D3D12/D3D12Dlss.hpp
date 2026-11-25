#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Effects/SR/SuperResolution.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D12 {
namespace Dlss {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   NVSDK_NGX_Parameter* capabilities_params = nullptr;
   NVSDK_NGX_Parameter* runtime_params = nullptr;
   NVSDK_NGX_Handle* feature = nullptr;
   Hooks::Lifetime::D3D12::Object feature_lifetime;
};

bool Init(State& state, ID3D12Device* device);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D12GraphicsCommandList* list, const SettingsData& settings);
bool Evaluate(State& state, ID3D12GraphicsCommandList* list, const DrawInputs<ID3D12Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D12GraphicsCommandList, ID3D12Resource>);
} // namespace Dlss
} // namespace D3D12
} // namespace SR
} // namespace Effects
} // namespace Luma
