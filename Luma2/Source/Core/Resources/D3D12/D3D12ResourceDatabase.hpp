#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <d3d12.h>

#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceDatabase.hpp"

namespace Luma {
namespace Resources {
namespace Database {
namespace D3D12 {
using TableLayout = std::array<uint32_t, Bindings::D3D12_TEXTURE_REGISTERS>;
inline constexpr uint32_t NO_TABLE = UINT32_MAX;
// Any thread; returns NO_TABLE when no fixed tail slot remains
uint32_t RegisterTable(const TableLayout& layout);

void CreateDeviceObjects(ID3D12Device* device);
void ReleaseDeviceObjects(ID3D12Device* device);

void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature);
void SetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline);
void SetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table);
void SetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address);
void SetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil);
void ForgetList(ID3D12GraphicsCommandList* list);
void SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps);
// Restores the game's tracked heaps, root signatures, root arguments, pipeline state, and render targets after an SR/custom pass
void RestoreListState(ID3D12GraphicsCommandList* list);
// Returns false when the texture table can't be bound (caller falls back to game's shaders)
bool BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom = {}, uint32_t table = NO_TABLE);
void BindForBundle(ID3D12GraphicsCommandList* list);
ID3D12DescriptorHeap* GetBoundHeap(ID3D12GraphicsCommandList* list);

struct TextureCopyOptions
{
   DXGI_FORMAT format_override = DXGI_FORMAT_UNKNOWN;
   D3D12_RESOURCE_FLAGS extra_flags = D3D12_RESOURCE_FLAG_NONE;
};

struct BoundCBuffer
{
   D3D12_GPU_VIRTUAL_ADDRESS address = 0;
   uint32_t size = 0; // 0 if root CBV (unknown size)
};

// Bound-resource queries using the list's tracked root signature, root arguments, descriptors, and mirrors
ID3D12Resource* FindBoundSRV(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot);
ID3D12Resource* FindBoundRTV(ID3D12GraphicsCommandList* list, UINT slot);
ID3D12Resource* FindBoundDSV(ID3D12GraphicsCommandList* list);
std::optional<BoundCBuffer> FindBoundCBuffer(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot);

// RuntimeTexture entries: zero-copy per-frame tags (cleared at OnPresent) and persistent managed textures
void TagResource(ID3D12Device* device, uint64_t name_hash, ID3D12Resource* resource);
ID3D12Resource* TagBoundSRV(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot, uint64_t name_hash);
ID3D12Resource* TagBoundRTV(ID3D12GraphicsCommandList* list, UINT slot, uint64_t name_hash);
ID3D12Resource* TagBoundDSV(ID3D12GraphicsCommandList* list, uint64_t name_hash);
ID3D12Resource* EnsureTexture(ID3D12Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
   D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON);
ID3D12Resource* GetResource(ID3D12Device* device, uint64_t name_hash);
ID3D12Resource* ReferenceResource(ID3D12GraphicsCommandList* list, uint64_t name_hash);
D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(ID3D12Device* device, uint64_t name_hash);
D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(ID3D12Device* device, uint64_t name_hash);
D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(ID3D12Device* device, uint64_t name_hash);
D3D12_RESOURCE_STATES GetManagedState(ID3D12Device* device, uint64_t name_hash);
void SetManagedState(ID3D12Device* device, uint64_t name_hash, D3D12_RESOURCE_STATES state);
void OnPresent(ID3D12Device* device);
void Invalidate(ID3D12Device* device, Invalidation reason);

struct TableView
{
   ID3D12Resource* resource = nullptr;
   D3D12_SHADER_RESOURCE_VIEW_DESC desc = {}; // UNKNOWN dimension uses default view
};

using TableViews = std::array<TableView, Bindings::D3D12_TEXTURE_REGISTERS>;
// Caller keeps resources alive until the list has executed
bool BindTableViews(ID3D12GraphicsCommandList* list, bool compute, uint32_t table, const TableViews& views);

struct Stats
{
   uint64_t ring_bytes = 0;
   uint32_t chunks = 0;
   uint32_t textures = 0;
   uint32_t tables = 0;
   uint32_t view_tables = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
