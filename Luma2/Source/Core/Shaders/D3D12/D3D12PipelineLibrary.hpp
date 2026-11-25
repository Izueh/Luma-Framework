#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Shaders/D3D12/D3D12Database.hpp"

// Luma's ID3D12PipelineLibrary in Luma/Cache/Pipelines: the variant pipelines made on earlier boots, loaded instead of compiled
// again. Opened at the first use on a device; a file the driver rejects (another driver or adapter) is replaced by an empty one.
namespace Luma {
namespace Shaders {
namespace PipelineLibrary {
namespace D3D12 {
// Any thread: the pipeline stored under `name`, created from `desc` (of `kind`), or null
Microsoft::WRL::ComPtr<ID3D12PipelineState> Load(ID3D12Device* device, const std::wstring& name, Database::D3D12::PipelineKind kind, const void* desc);
// Any thread
void Store(ID3D12Device* device, const std::wstring& name, ID3D12PipelineState* pipeline);
// Any thread
void RecordFormats(uint64_t pipeline, const Database::D3D12::TargetFormats& formats);
// Any thread
std::vector<Database::D3D12::TargetFormats> GetFormats(ID3D12Device* device, uint64_t pipeline);
// Presenting thread, at every present: written to disk a few seconds after the last store, on a job
void Save();
// The game released the device
void ReleaseDeviceObjects(ID3D12Device* device);

struct Stats
{
   uint32_t hits = 0;
   uint32_t misses = 0;
   uint32_t stored = 0;
   uint32_t format_sets = 0;
};

Stats GetStats();
} // namespace D3D12
} // namespace PipelineLibrary
} // namespace Shaders
} // namespace Luma
