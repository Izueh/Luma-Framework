#pragma once

#if DEVELOPMENT
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "Game/GameEvents.hpp"
#include "Shaders/ShaderStages.hpp"
#include "Trace/FrameCapture.hpp"

// Trace capture on D3D11: a call's state from its context, before the game's draw callbacks run
namespace Luma {
namespace Trace {
namespace Capture {
namespace D3D11 {
// Event capture
struct Recording
{
   Event event;
   Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
   // Copies of the constant buffers the call reads (by index in `event.constant_buffers`), read back at the present
   std::vector<std::pair<size_t, Microsoft::WRL::ComPtr<ID3D11Buffer>>> copies;
};

// Inside a Luma scope, while capturing. The viewports, blend and depth state of game calls come from the bound-state cache.
Recording BeginDraw(ID3D11DeviceContext* context, const Game::D3D11::DrawCall& draw);
Recording BeginDispatch(ID3D11DeviceContext* context, const Game::D3D11::DispatchCall& dispatch);
// Luma's own pass, after it set its state: `shader` is its custom shader's "name"_h
Recording BeginPass(ID3D11DeviceContext* context, std::string_view name, ShaderStage stage, uint64_t shader);
void End(Recording recording, Outcome outcome = Outcome::Ran);

// While capturing, as the game made the call (before Luma redirects it to mirrors): a copy or a resolve, a view cleared, a resource
// written by the CPU. `name` is the native function's.
void AddCopy(ID3D11DeviceContext* context, std::string_view name, ID3D11Resource* destination, ID3D11Resource* source, const Copy& copy);
void AddClear(ID3D11DeviceContext* context, std::string_view name, ID3D11View* view, const std::array<float, 4>& values);
void AddWrite(ID3D11DeviceContext* context, std::string_view name, ID3D11Resource* resource, const CpuAccess& access = {}, EventType type = EventType::CpuWrite);
void AddSrPass(ID3D11DeviceContext* context, const SrInfo& info, ID3D11Resource* color, ID3D11Resource* depth, ID3D11Resource* motion_vectors,
   ID3D11Resource* exposure, ID3D11Resource* reactive, ID3D11Resource* transparency, ID3D11Resource* output);

// Cbuffer tracking and shader skipping
// The trace's action on a shader bound to one of `stages` (a call's) skips the call
bool IsSkipped(ID3D11DeviceContext* context, Shaders::StageSet stages);
// While Track is active, before the game's draw callbacks: copies the tracked slot if this is the tracked call (`stages` are the
// call's)
void TrackCall(ID3D11DeviceContext* context, Shaders::StageSet stages);
// While Track is active: a deferred context's copy belongs to the command list it ended in, and is read once that list was executed
void FinishCommandList(const ID3D11DeviceContext* context, const ID3D11CommandList* list);
void MergeCommandList(ID3D11DeviceContext* context, const ID3D11CommandList* list);

// Present readbacks
// Presenting thread: the newest copy of the tracked slot whose contents are there read back into Track (none: the reading is stale)
void ReadBackTracked();
// Presenting thread, before Capture::FinishFrame: the copies read back into their events
void ReadBackConstantBuffers();
} // namespace D3D11
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
