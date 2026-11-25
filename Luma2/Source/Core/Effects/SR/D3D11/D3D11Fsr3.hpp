#pragma once

#include <d3d11.h>

#include "Effects/SR/SuperResolution.hpp"

namespace Luma {
namespace Effects {
namespace SR {
namespace D3D11 {
namespace Fsr3 {
struct State
{
   Capabilities capabilities;
   SettingsData settings;
   void* context_storage = nullptr;
   void* scratch_buffer = nullptr;
   bool has_context = false;
   int phase_count = 16;
};

bool Init(State& state, ID3D11Device* device);
void Deinit(State& state);
void ReleaseResources(State& state);
bool UpdateSettings(State& state, ID3D11DeviceContext* context, const SettingsData& settings);
bool Evaluate(State& state, ID3D11DeviceContext* context, const DrawInputs<ID3D11Resource>& inputs, const DrawParams& params);
int GetJitterPhases(const State& state);

static_assert(SrBackend<State, ID3D11DeviceContext, ID3D11Resource>);
} // namespace Fsr3
} // namespace D3D11
} // namespace SR
} // namespace Effects
} // namespace Luma
