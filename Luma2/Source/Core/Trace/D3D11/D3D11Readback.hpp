#pragma once

#if DEVELOPMENT
#include <d3d11.h>
#include <dxgi.h>

#include "Game/GameEvents.hpp"
#include "Shaders/ShaderStages.hpp"

// Trace::Readback on D3D11: the armed view is copied on the context right after the armed call ran, and read at the next present
namespace Luma {
namespace Trace {
namespace Readback {
namespace D3D11 {
// While armed, after the call's events and fulfil step (inside a Luma scope): at the armed call, runs it unless it was already handled
// (run, skipped or replaced by a callback) and copies the view. Whether the call was handled.
bool ReadAfterCall(const Game::D3D11::Call& call, Shaders::StageSet stages, bool handled);
// Presenting thread, at a D3D11 present, before the overlay: the copy (or the back buffer) read back into the result
void ReadBack(IDXGISwapChain* swapchain, ID3D11Device* device);
} // namespace D3D11
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
