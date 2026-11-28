#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

// Draw on D3D12: per-command-list recorded graphics state, save/restore, temporary list-owned targets, and Luma's copy/scale passes.
namespace Luma {
namespace Draw {
namespace D3D12 {
// What the command list currently has bound, recorded at bind time so Luma's passes can restore it without asking the API.
struct Recorded
{
   ID3D12RootSignature* root_signature = nullptr;
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets = {};
   UINT render_target_count = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE depth = {};
   bool has_depth = false;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
   UINT viewport_count = 0;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors = {};
   UINT scissor_count = 0;
   D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

const Recorded* GetRecorded(ID3D12GraphicsCommandList* list);

void RecordRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature);
void RecordRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil);
void RecordViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports);
void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects);
void RecordTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology);
void ForgetList(ID3D12GraphicsCommandList* list);

// Inside a Luma scope: restores the list's recorded render targets, viewports, scissors, topology, and pipeline.
void Restore(ID3D12GraphicsCommandList* list);
// The game released the device: Luma's cached copy/scale pipelines and RTV heap on it go.
void ReleaseDeviceObjects(ID3D12Device* device);

struct CopyOptions
{
   UINT destination_subresource = 0;
   UINT source_subresource = 0;
   D3D12_RESOURCE_STATES destination_state = D3D12_RESOURCE_STATE_COPY_DEST;
   D3D12_RESOURCE_STATES source_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
};

// `source` into the whole subresource of `destination`, using "Luma_Copy_VS/Table_PS" (point) or "Luma_Scale_VS/Table_PS" (bilinear).
// Transitions both resources around the draw and restores the list's recorded state.
bool Copy(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyOptions& options = {});
bool Scale(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyOptions& options = {});

// A committed texture of Luma's kept alive on `list` until it has executed on a queue.
ID3D12Resource* CreateTemporary(ID3D12GraphicsCommandList* list, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state);
ID3D12Resource* CreateTemporary2D(ID3D12GraphicsCommandList* list, DXGI_FORMAT format, uint32_t width, uint32_t height, D3D12_RESOURCE_FLAGS flags,
   D3D12_RESOURCE_STATES state);
} // namespace D3D12
} // namespace Draw
} // namespace Luma
