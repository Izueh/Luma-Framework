#pragma once

#if DEVELOPMENT
#include <cstdint>

#include <d3d12.h>

#include "Luma.hpp"
#include "Shaders/Stages.hpp"
#include "Trace/Capture.hpp"

// Trace capture on D3D12. A command list records what the game sets on it (root signatures and arguments, render targets,
// viewports, topology), and its calls' events while a capture runs; the events go into the frame when a queue executes the list,
// in submission order. A call's state is read through the root layout: its tables' descriptors (Resources::Descriptors::D3D12),
// its root descriptors by GPU address, its constants; the fixed-function state from the pipeline's desc. Development builds only.
namespace Luma {
namespace Trace {
namespace Capture {
namespace D3D12 {
// The list's record, from the game's calls (Luma's own aren't hooked)
void OnReset(ID3D12GraphicsCommandList* list);
void OnClearState(ID3D12GraphicsCommandList* list);
void OnSetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
void OnSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil);
void OnSetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table);
void OnSetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address);
void OnSetRootConstants(ID3D12GraphicsCommandList* list, bool compute, UINT index, UINT count, const void* values, UINT offset);
void OnSetViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports);
void OnSetScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects);
void OnSetTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology);
void OnSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4]);
void OnSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref);

struct Recording
{
   Event event;
   ID3D12GraphicsCommandList* list = nullptr;
};

// While capturing, before the game's draw events
Recording BeginDraw(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw);
Recording BeginDispatch(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch);
// Kept by the list until a queue executes it
void End(Recording recording, Outcome outcome = Outcome::Ran);
// The bundle's events go into the list where it runs
void OnExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle);
// The lists' events go into the frame, in this order
void OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

// While Track is active, before the game's draw events: copies the tracked slot if this is the tracked call (from an upload heap)
void TrackCall(ID3D12GraphicsCommandList* list, Shaders::StageSet stages);
// The trace's action on a shader of the call's pipeline (at one of `stages`) skips the call
bool IsSkipped(ID3D12GraphicsCommandList* list, Shaders::StageSet stages);
// Presenting thread, at a D3D12 present: the last copy of the tracked slot this frame into Track (none: the reading is stale)
void ReadBackTracked();
} // namespace D3D12
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
