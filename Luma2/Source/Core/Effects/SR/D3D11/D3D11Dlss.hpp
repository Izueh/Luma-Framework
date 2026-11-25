#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Effects/SR/SuperResolution.hpp"

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace Dlss {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   Microsoft::WRL::ComPtr<ID3D11Device> device;
   Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
   NVSDK_NGX_Parameter* capabilities_params = nullptr;
   NVSDK_NGX_Parameter* runtime_params = nullptr;
   NVSDK_NGX_Handle* feature = nullptr;
};

bool Init(State& state, ID3D11Device* device);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D11DeviceContext* context, const SettingsData& settings);
bool Evaluate(State& state, ID3D11DeviceContext* context, const DrawInputs<ID3D11Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D11DeviceContext, ID3D11Resource>);
} // namespace Dlss
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
