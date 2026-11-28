#pragma once

#if DEVELOPMENT
#include <d3d12.h>
#include <dxgi.h>

#include "Game/GameEvents.hpp"
#include "Shaders/ShaderStages.hpp"

// Trace::Readback on D3D12: right after the armed call, its list transitions the view's resource to a copy source, copies it into a
// readback buffer and transitions it back; once a queue executed the list, the present waits for it and reads the buffer. The back
// buffer is copied at present on the presenting queue. Render targets and UAVs are in a known state at a call; a shader resource
// needs the list to have moved it there with a barrier. Depth, stencil and multisampled textures aren't read on D3D12 yet.
namespace Luma {
namespace Trace {
namespace Readback {
namespace D3D12 {
// While armed, after the call's events and the variant's requests (inside a Luma scope): at the armed call, runs it unless it was
// already handled and records the copy. Whether the call was handled.
bool ReadAfterCall(const Game::D3D12::Call& call, Shaders::StageSet stages, bool handled);
// After ExecuteCommandLists: the queue the armed copy's list ran on
void CommitExecuted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
// Presenting thread, at a D3D12 present, before the overlay: the copy (or the back buffer) read back into the result
void ReadBack(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue);
} // namespace D3D12
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
