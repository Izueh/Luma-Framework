#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Common/ShaderStage.hpp"
#include "Shaders/ShaderDatabase.hpp"

// The game's pipelines and root signatures on D3D12, each in its object's private data: a pipeline's shaders are the same
// Database::Shader as D3D11's (by hash), a root signature's layout is what its extension and trace read.
namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D12 {
struct RootRange
{
   D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
   uint32_t count = 0; // UINT_MAX for an unbounded range
   uint32_t base_register = 0;
   uint32_t space = 0;
   uint32_t offset = 0; // In the table, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND resolved
};

struct RootParameter
{
   D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_CBV;
   D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
   // Root descriptors and constants
   uint32_t shader_register = 0;
   uint32_t space = 0;
   uint32_t constant_count = 0;
   // Tables
   std::vector<RootRange> ranges;
};

struct RootLayout
{
   D3D_ROOT_SIGNATURE_VERSION version = D3D_ROOT_SIGNATURE_VERSION_1_0; // As the game serialized it
   D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
   std::vector<RootParameter> parameters;
   uint32_t static_samplers = 0; // The game's
   std::vector<D3D12_STATIC_SAMPLER_DESC> static_sampler_descs; // The game's, then Luma's in an extended signature
   uint32_t size = 0; // In DWORDs: a table is 1, a root descriptor 2, constants one each
   std::vector<uint8_t> blob; // The game's, what an extension starts from
   std::atomic<uint32_t> pipelines = 0; // Alive, created with it
   // The first of Luma's root parameters (Shaders::RootSignatures::D3D12), UINT32_MAX for a signature that wasn't extended
   uint32_t luma_parameter = UINT32_MAX;
   bool luma_table = false; // Luma's texture table follows its cbuffers (Shaders::RootSignatures::D3D12::LUMA_TABLE_PARAMETER)
};

enum class PipelineKind : uint8_t
{
   Graphics,
   Compute,
   Stream, // ID3D12Device2::CreatePipelineState
};

// A pipeline desc copied with everything it points to, so a variant can be created from it any time later (the game's memory is
// gone by then)
struct DescCopy
{
   PipelineKind kind = PipelineKind::Graphics;
   // The desc struct, or the stream. Its pointers point into the storage, the root signature excepted (the pipeline holds it).
   std::vector<std::byte> bytes;
   std::vector<std::unique_ptr<std::byte[]>> storage;
   // Where each stage's D3D12_SHADER_BYTECODE is in the bytes (ShaderStage order), SIZE_MAX for a stage it doesn't have
   std::array<size_t, 6> stages = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
   // Of everything but the shaders' bytecode and the root signature object, for Luma's pipeline library
   uint64_t content_hash = 0;
};

// UNKNOWN leaves the pipeline's original format unchanged
struct TargetFormats
{
   std::array<DXGI_FORMAT, 8> render_targets = {};
   DXGI_FORMAT depth = DXGI_FORMAT_UNKNOWN;

   bool Any() const;
   friend bool operator==(const TargetFormats&, const TargetFormats&) = default;
};

struct Pipeline
{
   PipelineKind kind = PipelineKind::Graphics;
   bool from_library = false; // Loaded from the game's ID3D12PipelineLibrary
   std::array<Shader*, 6> shaders = {}; // ShaderStage order
   Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
   const RootLayout* root_layout = nullptr; // Null for one Luma didn't see created
   bool embedded_root_signature = false; // No root signature in the desc: it's in the bytecode
   // What variants are created from: copied when the pipeline may get one (Development: always)
   std::shared_ptr<const DescCopy> desc;
};

// The bytecode of each stage of a pipeline desc, the root signature it names
struct PipelineDesc
{
   PipelineKind kind = PipelineKind::Graphics;
   std::array<std::span<const uint8_t>, 6> stages;
   ID3D12RootSignature* root_signature = nullptr;
   // A stream desc with a subobject type this build doesn't know: what came before it was read
   bool unknown_subobject = false;
   bool mesh_shaders = false;
};

// A stream subobject is `struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; T value; }` (d3dx12's
// CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT): its value's offset and its whole size. None for a type this build doesn't know.
struct SubobjectLayout
{
   size_t value_offset = 0;
   size_t size = 0;
};

std::optional<SubobjectLayout> GetSubobjectLayout(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type);

PipelineDesc DescribePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
PipelineDesc DescribePipeline(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc);
PipelineDesc DescribePipeline(const D3D12_PIPELINE_STATE_STREAM_DESC& desc);

// Any thread; `luma_parameter` is UINT32_MAX when unextended
const RootLayout* TrackRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter, bool luma_table,
   bool luma_samplers = false);
const RootLayout* FindRootLayout(ID3D12RootSignature* root_signature);

// Any thread: a deep copy of a native desc of `kind` (D3D12_GRAPHICS_PIPELINE_STATE_DESC, D3D12_COMPUTE_PIPELINE_STATE_DESC or
// D3D12_PIPELINE_STATE_STREAM_DESC). Null for a stream with a subobject this build doesn't know.
std::shared_ptr<const DescCopy> CopyDesc(PipelineKind kind, const void* desc);

// A graphics pipeline's fixed-function state, which D3D12 bakes into the pipeline. A stream without a subobject has its default.
struct GraphicsState
{
   D3D12_BLEND_DESC blend = {};
   D3D12_RASTERIZER_DESC rasterizer = {};
   float depth_bias = 0.f; // D3D12_RASTERIZER_DESC1 and 2 have a float bias
   D3D12_DEPTH_STENCIL_DESC depth_stencil = {};
   D3D12_RT_FORMAT_ARRAY targets = {};
   DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
   D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
   UINT sample_count = 1;
};

// None for a compute pipeline
std::optional<GraphicsState> GetGraphicsState(const DescCopy& desc);

// Any thread: the game created (or loaded) `pipeline` from `desc`, `copy` (null when not copied) its deep copy
const Pipeline* TrackPipeline(const PipelineDesc& desc, std::shared_ptr<const DescCopy> copy, bool from_library, ID3D12PipelineState* pipeline);
const Pipeline* FindPipeline(ID3D12PipelineState* pipeline);

struct Stats
{
   uint32_t pipelines = 0; // The game's, alive
   uint32_t extended_signatures = 0; // Created so far
   uint32_t unextended_signatures = 0; // Created as the game's: local, over the size limit, a conflict, refused, or Luma's shaders not needed
   uint32_t largest_extended_dwords = 0;
   std::vector<uint32_t> unextended_dwords; // First 64
};

// Any thread
Stats GetStats();
} // namespace D3D12
} // namespace Database
} // namespace Shaders
} // namespace Luma
