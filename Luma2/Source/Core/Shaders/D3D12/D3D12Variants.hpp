#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>

#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Stages.hpp"
#include "Shaders/Variants.hpp"

// Variant pipelines: a game pipeline with each replaced stage's bytecode swapped for the variant D3D11 would bind (the published
// one of highest priority, or the trace's forced choice), and the same, extended, root signature. Swapped in at SetPipelineState.
//
// When they're made: when the game creates a pipeline whose desc was copied (Development, or a stage with a replacement file) and a
// stage already has a compiled variant; and at the present after a build publishes variants or the trace forces a choice, for each
// pipeline whose choice changed. From Luma's pipeline library at once when it has one, otherwise on a job. Until it's published
// the game's pipeline runs. A pipeline is recreated only when the bytecode it's made from changes.
namespace Luma {
namespace Shaders {
namespace Variants {
namespace D3D12 {
// What a variant pipeline was made from
struct Built
{
   std::array<std::shared_ptr<const CompiledShader>, 6> compiled; // Null for the game's stage
   std::array<Source, 6> sources = {};
   StageSet variant_stages;
   bool compute = false;
   // The Resources::Database entries its shaders request, once each
   std::vector<uint32_t> entries;
   IUnknown* object = nullptr; // The variant pipeline, to compare only
};

// Whether a pipeline's desc is copied at its creation: always in Development (files can be added while the game runs), otherwise
// when a stage has a replacement file
bool NeedsDescCopy(const Database::D3D12::PipelineDesc& desc);
// After Database::D3D12::TrackPipeline: the pipeline gets its variant when one is chosen
void AttachVariants(ID3D12PipelineState* pipeline, const Database::D3D12::Pipeline& tracked);
// The game binds `pipeline` (null unbinds): its variant goes in its place while Luma is enabled and the variants are loaded, and
// the list's record follows
void SwapInVariant(ID3D12GraphicsCommandList* list, ID3D12PipelineState*& pipeline);
// The list was reset, or bound a state object: no variant is bound
void ForgetBound(ID3D12GraphicsCommandList* list);
// The variant bound on the list, null for none. Valid until the list's next bind.
const Built* GetBound(ID3D12GraphicsCommandList* list);

// What the list runs: the game's pipeline it bound (null for none, or a state object) and its record, and the variant in its place
struct BoundPipeline
{
   ID3D12PipelineState* game = nullptr;
   const Database::D3D12::Pipeline* tracked = nullptr; // Null for one Luma didn't see created
   const Built* built = nullptr; // Null when the game's pipeline runs
};
BoundPipeline GetBoundPipeline(ID3D12GraphicsCommandList* list);
// Binds the game's pipeline in place of the variant until the game binds another (a draw event's UseOriginal)
void UseOriginal(ID3D12GraphicsCommandList* list);
// Presenting thread, at a D3D12 present: pipelines whose choice changed (a publication, a forced choice) are made again
void UpdateVariants();

struct Stats
{
   uint32_t pipelines = 0; // With a desc copy
   uint32_t ready = 0;
   uint32_t pending = 0;
   uint32_t failed = 0;
};

// Any thread
Stats GetStats();
} // namespace D3D12
} // namespace Variants
} // namespace Shaders
} // namespace Luma
