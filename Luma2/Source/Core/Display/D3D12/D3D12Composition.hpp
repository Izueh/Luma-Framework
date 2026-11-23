#pragma once

#include <memory>

#include <d3d12.h>

#include "Display/DisplayComposition.hpp"

// Display composition on D3D12: Luma's own command list on the queue that presents, with its own root signature (the shader's
// cbuffers as root views in their space, its views from a heap of its own, a static linear sampler) and pipeline. One renderer per
// swapchain, since a swapchain has one queue.
namespace Luma {
namespace Display {
namespace Composition {
namespace D3D12 {
struct Renderer;

struct RendererDeleter
{
   // Waits for its work
   void operator()(Renderer* renderer) const;
};

using RendererPtr = std::unique_ptr<Renderer, RendererDeleter>;

// Null on failure
RendererPtr CreateRenderer(ID3D12CommandQueue* queue);
// Before the real buffers are resized: nothing of Luma's uses them any more
void WaitIdle(Renderer& renderer);

// Inside a Luma scope, on the queue: `source` (the game's image, in the common state, as a back buffer is at present) into the whole of
// `target` (a real back buffer, in the present state); both are left as they were. `source` equal to `target` composes a copy of it
// (in place). False when Luma's shaders aren't compiled yet.
bool Compose(Renderer& renderer, ID3D12Resource* source, ID3D12Resource* target, const Pass& pass);
} // namespace D3D12
} // namespace Composition
} // namespace Display
} // namespace Luma
