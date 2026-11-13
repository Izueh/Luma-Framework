#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "Luma.hpp"
#include "Trace/Capture.hpp"

// Trace capture on D3D11: a call's state from its context, before the game's draw callbacks run
namespace Luma {
namespace Trace {
namespace Capture {
namespace D3D11 {
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

// Presenting thread, before Capture::OnPresent: the copies read back into their events
void ReadBackConstantBuffers();

// While Track is active, before the game's draw callbacks: copies the tracked slot if this is the tracked call (`stages` are the
// call's, Draw::GetStageBit bits)
void TrackCall(ID3D11DeviceContext* context, uint8_t stages);
// While Track is active: a deferred context's copy belongs to the command list it ended in, and is read once that list was executed
void OnFinishCommandList(const ID3D11DeviceContext* context, const ID3D11CommandList* list);
void OnExecuteCommandList(ID3D11DeviceContext* context, const ID3D11CommandList* list);
// Presenting thread: the newest copy of the tracked slot whose contents are there read back into Track (none: the reading is stale)
void ReadBackTracked();
} // namespace D3D11
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
