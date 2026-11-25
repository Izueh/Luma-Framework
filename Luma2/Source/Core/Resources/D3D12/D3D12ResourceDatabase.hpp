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
void ForgetList(ID3D12GraphicsCommandList* list);
void SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps);
// Returns false when the texture table can't be bound (caller falls back to game's shaders)
bool BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom = {}, uint32_t table = NO_TABLE);
void BindForBundle(ID3D12GraphicsCommandList* list);
ID3D12DescriptorHeap* GetBoundHeap(ID3D12GraphicsCommandList* list);

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
