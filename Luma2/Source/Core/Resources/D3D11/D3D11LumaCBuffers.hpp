#pragma once

#include <cstdint>

#include <d3d11.h>

#include "Common/ShaderStage.hpp"
#include "Shaders/Stages.hpp"

// Restoring the game's buffers at Luma's two cbuffer slots after a draw that bound Luma's there (Options::restore_after_draw).
// Only those two slots are recorded, per stage of each context.
namespace Luma {
namespace Resources {
namespace LumaCBuffers {
namespace D3D11 {
// The game binds constant buffers: what lands at Luma's slots is recorded (Luma's own binds, in a Luma scope, aren't seen)
void RecordGameBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first_constants,
   const UINT* constant_counts);
// The context's state went back to the defaults (ClearState, or a command list finished or executed without restoring it)
void ForgetGameBuffers(ID3D11DeviceContext* context);
// Inside a Luma scope, after the draw: the game's buffers at Luma's slots on `stages`, as it bound them
void RestoreGameBuffers(ID3D11DeviceContext* context, Shaders::StageSet stages);
} // namespace D3D11
} // namespace LumaCBuffers
} // namespace Resources
} // namespace Luma
