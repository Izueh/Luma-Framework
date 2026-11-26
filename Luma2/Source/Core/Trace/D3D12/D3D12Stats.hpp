#pragma once

#if DEVELOPMENT
#include <filesystem>

#include <d3d12.h>

#include "Hooks/HookFunctions.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"

// What a game does on D3D12, counted for research: pipelines, root signatures, descriptor heaps and
// writes, the bind order before draws. Development builds only. Counted all the time (cheaply), written to Luma/Dump/D3D12Stats.json only when
// asked (MCP's dump_stats, the Info tab) and with each frame capture: luma_status.d3d12 has the live counters.
namespace Luma {
namespace Trace {
namespace Stats {
namespace D3D12 {
// Every function it counts
Hooks::D3D12::Functions GetD3D12Functions();

void RecordDevice(ID3D12Device* device);
void RecordRootSignature(const Shaders::Database::D3D12::RootLayout& layout);
void RecordPipeline(ID3D12Device* device, const Shaders::Database::D3D12::Pipeline& pipeline, const Shaders::Database::D3D12::PipelineDesc& desc);
void RecordPipelineLibrary(size_t blob_size, HRESULT result);
void RecordLibraryLoad(HRESULT result);
void RecordHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc, ID3D12DescriptorHeap* heap);
void RecordViewWrite(D3D12_CPU_DESCRIPTOR_HANDLE destination);
void RecordCopy(D3D12_CPU_DESCRIPTOR_HANDLE destination, UINT count);

enum class Bind : uint8_t
{
   RootSignature = 1,
   PipelineState = 2,
   RenderTargets = 3,
};

void RecordReset(ID3D12GraphicsCommandList* list);
void RecordBind(ID3D12GraphicsCommandList* list, Bind bind);
void RecordDraw(ID3D12GraphicsCommandList* list, bool dispatch);
void RecordExecuteIndirect();
void RecordExecuteBundle();
void RecordExecute(UINT count);

// Presenting thread
void CountFrame();
// Presenting thread: the report written now, on a job. Its path.
std::filesystem::path Write();
// Any thread: written at the next present
void RequestWrite();
// A D3D12 device was seen: there is something to write
bool HasData();
} // namespace D3D12
} // namespace Stats
} // namespace Trace
} // namespace Luma
#endif
